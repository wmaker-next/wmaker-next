/*
 *  wmsoundd - Window Maker sound event player daemon
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 *  Subscribes to Window Maker's WIPC sound event stream and plays the
 *  sound file mapped to each event in the WMSound defaults domain. Links
 *  libWUtil (proplist + WIPC), libsndfile and PipeWire, no X11.
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_INOTIFY
#include <sys/inotify.h>
#endif

#pragma GCC diagnostic push
/* older PipeWire headers use %u for __LINE__ in a static inline helper */
#pragma GCC diagnostic ignored "-Wformat"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#pragma GCC diagnostic pop
#include <sndfile.h>

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>

#define PROG "wmsoundd"
#define WMSOUND_DOMAIN "WMSound"
#define MAX_SOUND_PATH 32
#define RECONNECT_BACKOFF_MS 250
#define RECONNECT_MAX_MS 8000
#define RECONNECT_STABLE_MS 5000
#define CONNECT_GIVEUP_MS (10 * 60 * 1000)
#define DRAIN_TIMEOUT_MS (10 * 1000)
#define MAX_SOUND_FILE_BYTES (16 * 1024 * 1024)
#define MAX_PCM_BYTES (64 * 1024 * 1024)

/* Wire-name to domain-key table, from <WINGs/WIPC.h> */
static const struct {
	const char *wire;
	const char *key;
} event_map[] = {
#define WSOUND_ITEM(id, wire, key) { wire, key },
	WSOUND_EVENTS(WSOUND_ITEM)
#undef WSOUND_ITEM
};

static struct {
	struct pw_thread_loop *thread_loop;
	struct pw_context *context;
	struct pw_core *core;
	struct pw_stream *stream;
	struct spa_hook stream_listener;

	const void *samples;
	size_t sample_size;
	size_t play_offset;
	int channels;
	int rate;
} pw_state;

static struct {
	WMPropList *domain;
	char *domain_path;
	/* atomic, written by domain_load() and read by the playback callback */
	atomic_int volume_scale;
#ifdef HAVE_INOTIFY
	int inotify_fd;
	int inotify_wd;
#endif
} g = {
	.volume_scale = 65536,
#ifdef HAVE_INOTIFY
	.inotify_fd = -1,
	.inotify_wd = -1
#endif
};

static volatile sig_atomic_t shutdown_pending;
static volatile sig_atomic_t sighup_pending;

/* signal-to-poll wakeup: the handler writes one byte so the poll in
 * ipc_loop() cannot miss a signal delivered between the pending-flag
 * check and the syscall */
static int sig_pipe[2] = { -1, -1 };

static long long monotonic_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void usage(FILE *out)
{
	fprintf(out,
		"Usage: " PROG " [-s SOCKET] [-d DISPLAY] [--debug] [-h]\n"
		"       " PROG " --version|-v\n"
		"  -s SOCKET     connect to SOCKET instead of the resolved path\n"
		"  -d DISPLAY    resolve the IPC socket for DISPLAY (default $DISPLAY)\n"
		"  --debug       print runtime diagnostics to stderr\n"
		"  -h, --help    show this message\n"
		"  --version, -v print version information and exit\n");
}

static void on_process(void *userdata)
{
	struct pw_buffer *b;
	struct spa_buffer *buf;
	uint32_t n_frames, frame_size, i;
	int16_t *dst;
	int ch;
	int32_t scale;

	(void)userdata;

	if (!pw_state.samples)
		return;

	if ((b = pw_stream_dequeue_buffer(pw_state.stream)) == NULL) {
		pw_log_warn("out of buffers: %m");
		return;
	}
	buf = b->buffer;
	n_frames = b->requested ? b->requested : 1024;
	frame_size = 2 * pw_state.channels;

	if ((size_t)n_frames * frame_size > buf->datas[0].maxsize)
		n_frames = buf->datas[0].maxsize / frame_size;

	dst = buf->datas[0].data;
	scale = atomic_load_explicit(&g.volume_scale, memory_order_relaxed);
	for (i = 0; i < n_frames; i++) {
		size_t off = pw_state.play_offset;
		if (off + frame_size > pw_state.sample_size)
			break;
		for (ch = 0; ch < pw_state.channels; ch++) {
			const int16_t *src = (const int16_t *)
				((const uint8_t *)pw_state.samples + off + ch * 2);
			int32_t scaled = (*src * scale) >> 16;

			if (scaled > 32767)
				scaled = 32767;
			if (scaled < -32768)
				scaled = -32768;
			dst[i * pw_state.channels + ch] = (int16_t)scaled;
		}
		pw_state.play_offset += frame_size;
	}
	buf->datas[0].chunk->offset = 0;
	buf->datas[0].chunk->stride = frame_size;
	buf->datas[0].chunk->size = i * frame_size;
	b->size = i;
	pw_stream_queue_buffer(pw_state.stream, b);

	if (pw_state.play_offset >= pw_state.sample_size)
		/* Playback done, next on_process queues a zero-size buffer */
		pw_state.play_offset = pw_state.sample_size = 0;
}

static const struct pw_stream_events stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.process = on_process,
};

/* Decode an audio file via libsndfile (WAV, AIFF, AU, FLAC, Ogg/Vorbis,
 * Ogg/Opus, MP3). Returns a wmalloc'd interleaved 16-bit PCM buffer the
 * caller must wfree, sets channels/rate/size. Always outputs S16_LE so
 * the PipeWire stream uses one format regardless of input */
static void *sound_decode(const char *path, int *out_channels,
			  int *out_rate, size_t *out_size)
{
	SNDFILE *sndfile;
	SF_INFO sfinfo;
	struct stat st;
	short *samples;
	sf_count_t frames, got;
	int fd;

	fd = open(path, O_RDONLY | O_NOFOLLOW);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)
	    || st.st_size > MAX_SOUND_FILE_BYTES) {
		close(fd);
		return NULL;
	}
	sfinfo.format = 0;
	sndfile = sf_open_fd(fd, SFM_READ, &sfinfo, SF_FALSE);
	if (!sndfile) {
		close(fd);
		return NULL;
	}
	if (sfinfo.channels < 1 || sfinfo.channels > 2 || sfinfo.frames <= 0) {
		sf_close(sndfile);
		close(fd);
		return NULL;
	}
	frames = sfinfo.frames;
	if (frames > MAX_PCM_BYTES /
	    (sfinfo.channels * (sf_count_t)sizeof(short))) {
		sf_close(sndfile);
		close(fd);
		return NULL;
	}
	samples = wmalloc(frames * sfinfo.channels * sizeof(short));
	got = sf_readf_short(sndfile, samples, frames);
	sf_close(sndfile);
	close(fd);
	if (got <= 0) {
		wfree(samples);
		return NULL;
	}
	*out_channels = sfinfo.channels;
	*out_rate = sfinfo.samplerate;
	*out_size = (size_t)got * sfinfo.channels * sizeof(short);
	return samples;
}

static void pw_play(const void *samples, size_t size, int channels, int rate)
{
	struct spa_pod_builder b;
	const struct spa_pod *params[1];
	uint8_t buffer[1024];
	struct pw_properties *props;

	if (pw_state.stream) {
		pw_stream_destroy(pw_state.stream);
		pw_state.stream = NULL;
	}
	if (pw_state.samples)
		wfree((void *)pw_state.samples);

	pw_state.samples = samples;
	pw_state.sample_size = size;
	pw_state.play_offset = 0;
	pw_state.channels = channels;
	pw_state.rate = rate;

	props = pw_properties_new(
		PW_KEY_MEDIA_TYPE, "Audio",
		PW_KEY_MEDIA_CATEGORY, "Playback",
		PW_KEY_MEDIA_ROLE, "Event",
		NULL);
	pw_state.stream = pw_stream_new(pw_state.core, PROG, props);
	if (!pw_state.stream) {
		fprintf(stderr, PROG ": cannot create the audio stream\n");
		wfree((void *)pw_state.samples);
		pw_state.samples = NULL;
		pw_state.sample_size = 0;
		return;
	}
	spa_zero(pw_state.stream_listener);
	pw_stream_add_listener(pw_state.stream, &pw_state.stream_listener,
			       &stream_events, NULL);

	spa_pod_builder_init(&b, buffer, sizeof(buffer));
	params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
		&SPA_AUDIO_INFO_RAW_INIT(
			.format = SPA_AUDIO_FORMAT_S16_LE,
			.channels = channels,
			.rate = rate));
	/* no RT_PROCESS: on_process must run under the thread-loop lock */
	if (pw_stream_connect(pw_state.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
			      PW_STREAM_FLAG_AUTOCONNECT |
			      PW_STREAM_FLAG_MAP_BUFFERS, params, 1) < 0) {
		fprintf(stderr, PROG ": cannot connect the audio stream\n");
		pw_stream_destroy(pw_state.stream);
		pw_state.stream = NULL;
		wfree((void *)pw_state.samples);
		pw_state.samples = NULL;
		pw_state.sample_size = 0;
	}
}

/* Try one SoundPath entry: expand it, append the relative file name, and
 * return the full path if the result is readable, else NULL. */
static char *try_path_entry(const char *dir, const char *rel)
{
	char *exp, *full;
	size_t len;

	exp = wexpandpath(dir);
	if (!exp)
		return NULL;
	len = strlen(exp) + 1 + strlen(rel) + 1;
	full = wmalloc(len);
	snprintf(full, len, "%s/%s", exp, rel);
	wfree(exp);
	if (access(full, R_OK) == 0)
		return full;
	wfree(full);
	return NULL;
}

/* Resolve a domain value to an absolute path. The user's data dir
 * (wuserdatapath()/Sounds, the directory wmaker.inst creates) is tried
 * first so a file dropped there is found regardless of layout; then the
 * SoundPath array from the WMSound domain is walked. */
static char *resolve_sound_file(WMPropList *value)
{
	WMPropList *sp, *item;
	const char *rel, *datapath;
	char *full, *soundsdir;
	int count, i;

	if (!value || !WMIsPLString(value))
		return NULL;
	rel = WMGetFromPLString(value);
	if (!rel || !*rel)
		return NULL;
	/* "None" (case-insensitive) is a no-op, matching the original
	 * WSoundServer semantics */
	if (strcasecmp(rel, "None") == 0)
		return NULL;
	/* absolute path is used as-is */
	if (rel[0] == '/')
		return wstrdup(rel);

	datapath = wuserdatapath();
	if (datapath && *datapath) {
		soundsdir = wstrconcat(datapath, "/Sounds");
		full = try_path_entry(soundsdir, rel);
		wfree(soundsdir);
		if (full)
			return full;
	}

	sp = WMGetFromPLDictionaryByName(g.domain, "SoundPath");
	if (!sp || !WMIsPLArray(sp))
		return NULL;
	count = WMGetPropListItemCount(sp);
	if (count > MAX_SOUND_PATH)
		count = MAX_SOUND_PATH;
	for (i = 0; i < count; i++) {
		item = WMGetFromPLArray(sp, i);
		if (!item || !WMIsPLString(item))
			continue;
		full = try_path_entry(WMGetFromPLString(item), rel);
		if (full)
			return full;
	}
	return NULL;
}

static const char *wire_to_key(const char *wire)
{
	size_t i;
	for (i = 0; i < sizeof(event_map) / sizeof(event_map[0]); i++)
		if (strcmp(event_map[i].wire, wire) == 0)
			return event_map[i].key;
	return NULL;
}

static void play_event(const char *event)
{
	WMPropList *key, *value;
	const char *domain_key;
	char *path;
	void *samples;
	int channels, rate;
	size_t size;

	domain_key = wire_to_key(event);
	if (!domain_key) {
		fprintf(stderr, PROG ": unknown event '%s'\n", event);
		return;
	}
	key = WMCreatePLString(domain_key);
	value = WMGetFromPLDictionary(g.domain, key);
	WMReleasePropList(key);
	path = resolve_sound_file(value);
	if (!path)
		return;
	samples = sound_decode(path, &channels, &rate, &size);
	wfree(path);
	if (!samples) {
		fprintf(stderr, PROG ": failed to decode '%s'\n", event);
		return;
	}
	pw_thread_loop_lock(pw_state.thread_loop);
	pw_play(samples, size, channels, rate);
	pw_thread_loop_unlock(pw_state.thread_loop);
}

static void domain_load(void)
{
	WMPropList *new_domain, *v;
	const char *vol_str;
	char *path, *gpath;

	path = wdefaultspathfordomain(WMSOUND_DOMAIN);
	if (!path) {
		fprintf(stderr, PROG ": cannot resolve %s domain path\n",
			WMSOUND_DOMAIN);
		return;
	}
	new_domain = WMReadPropListFromFile(path);
	if (!new_domain) {
		gpath = wglobaldefaultspathfordomain(WMSOUND_DOMAIN);
		wfree(path);
		path = gpath;
		new_domain = path ? WMReadPropListFromFile(path) : NULL;
	}
	if (!new_domain) {
		fprintf(stderr, PROG ": cannot read %s\n",
			path ? path : WMSOUND_DOMAIN);
		wfree(path);
		return;
	}
	if (g.domain)
		WMReleasePropList(g.domain);
	g.domain = new_domain;
	if (g.domain_path)
		wfree(g.domain_path);
	g.domain_path = path;

	v = WMGetFromPLDictionaryByName(g.domain, "Volume");
	vol_str = v ? WMGetFromPLString(v) : NULL;
	if (vol_str) {
		float fv = strtof(vol_str, NULL);

		if (fv >= 0.0f && fv <= 1.0f)
			atomic_store_explicit(&g.volume_scale,
					      (int)(fv * 65536.0f),
					      memory_order_relaxed);
	}
}

#ifdef HAVE_INOTIFY
/* Watch the directory, not the file inode: WMWritePropListToFile()
 * rename()s a temporary file over the target, orphaning a file watch */
static void inotify_watch_update(void)
{
	char *dir;

	if (g.inotify_fd < 0 || !g.domain_path)
		return;
	if (g.inotify_wd >= 0) {
		inotify_rm_watch(g.inotify_fd, g.inotify_wd);
		g.inotify_wd = -1;
	}
	dir = wpathdir(g.domain_path);
	g.inotify_wd = inotify_add_watch(g.inotify_fd, dir,
					 IN_CLOSE_WRITE | IN_MOVED_TO);
	wfree(dir);
}

/* Drain the event queue, 1 = an event for the current domain file arrived */
static int inotify_drain(void)
{
	union {
		char buf[4096];
		struct inotify_event align;
	} u;
	const struct inotify_event *ev;
	const char *name;
	char *p;
	ssize_t n;
	int reload = 0;

	name = g.domain_path ? strrchr(g.domain_path, '/') : NULL;
	name = name ? name + 1 : g.domain_path;
	if (!name || !*name)
		return 0;
	while ((n = read(g.inotify_fd, u.buf, sizeof(u.buf))) > 0) {
		p = u.buf;
		while (n >= (ssize_t)sizeof(*ev)) {
			ev = (const struct inotify_event *)p;
			if ((ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO))
			    && ev->len > 0
			    && strcmp(ev->name, name) == 0)
				reload = 1;
			n -= sizeof(*ev) + ev->len;
			p += sizeof(*ev) + ev->len;
		}
	}
	return reload;
}
#endif

static void on_signal(int sig)
{
	unsigned char byte = 1;
	ssize_t rc;

	if (sig == SIGTERM || sig == SIGINT)
		shutdown_pending = 1;
	else if (sig == SIGHUP)
		sighup_pending = 1;

	rc = write(sig_pipe[1], &byte, 1);
	(void)rc;
}

/* Give the PipeWire data thread time to finish the sample queued at
 * signal time, bounded so a stuck stream cannot hang shutdown */
static void drain_playback(void)
{
	long long deadline;
	int busy;

	deadline = monotonic_ms() + DRAIN_TIMEOUT_MS;
	for (;;) {
		pw_thread_loop_lock(pw_state.thread_loop);
		busy = pw_state.sample_size > 0
		       && pw_state.play_offset < pw_state.sample_size;
		pw_thread_loop_unlock(pw_state.thread_loop);
		if (!busy || monotonic_ms() >= deadline)
			return;
		usleep(20 * 1000);
	}
}

static int ipc_loop(int fd)
{
	uint32_t type, len;
	char *body;
	WMPropList *pl, *k, *v;
	const char *event;
	struct pollfd pfd[3];
	unsigned char sigbuf[64];
	int nfds, rc;
	ssize_t n;

	for (;;) {
		if (shutdown_pending)
			return 0;
		if (sighup_pending) {
			sighup_pending = 0;
			domain_load();
#ifdef HAVE_INOTIFY
			inotify_watch_update();
#endif
		}

		pfd[0].fd = fd;
		pfd[0].events = POLLIN;
		nfds = 1;
		pfd[nfds].fd = sig_pipe[0];
		pfd[nfds].events = POLLIN;
		nfds++;
#ifdef HAVE_INOTIFY
		if (g.inotify_fd >= 0) {
			pfd[nfds].fd = g.inotify_fd;
			pfd[nfds].events = POLLIN;
			nfds++;
		}
#endif
		/* poll so inotify and IPC wake the same loop; a signal
		 * wakes it through the self-pipe even when it lands just
		 * before the poll */
		rc = poll(pfd, nfds, -1);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (pfd[1].revents & POLLIN) {
			do {
				n = read(sig_pipe[0], sigbuf, sizeof(sigbuf));
			} while (n > 0);
			continue;
		}
#ifdef HAVE_INOTIFY
		if (nfds > 2 && (pfd[2].revents & POLLIN)) {
			if (inotify_drain())
				domain_load();
		}
#endif
		if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL))
			return -1;
		if (!(pfd[0].revents & POLLIN))
			continue;

		rc = wIPCRecv(fd, &type, &body, &len);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!(type & WIPC_EVENT_BIT) || !body) {
			wfree(body);
			continue;
		}
		if ((type & ~WIPC_EVENT_BIT) != WIPC_EV_SOUND) {
			wfree(body);
			continue;
		}
		pl = WMCreatePropListFromDescription(body);
		wfree(body);
		if (!pl)
			continue;
		k = WMCreatePLString("event");
		v = WMGetFromPLDictionary(pl, k);
		WMReleasePropList(k);
		event = (v && WMIsPLString(v)) ? WMGetFromPLString(v) : NULL;
		if (event)
			play_event(event);
		WMReleasePropList(pl);
	}
}

int main(int argc, char **argv)
{
	enum { OPT_DEBUG = 256 };
	const char *socket_path = NULL;
	const char *display = NULL;
	int fd, debug = 0, ch;
	char *path;
	unsigned backoff;
	long long started, giveup_deadline;
	int status = 0, rc, stable;
	struct sigaction sa;

	struct option longopts[] = {
		{ "help",    no_argument,       NULL, 'h' },
		{ "version", no_argument,       NULL, 'v' },
		{ "debug",   no_argument,       NULL, OPT_DEBUG },
		{ NULL,      0,                 NULL, 0 }
	};

	while ((ch = getopt_long(argc, argv, "hvs:d:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'h':
			usage(stdout);
			return 0;
		case 'v':
			printf(PROG " version %s\n", VERSION);
			return 0;
		case OPT_DEBUG:
			debug = 1;
			break;
		case 's':
			socket_path = optarg;
			break;
		case 'd':
			display = optarg;
			break;
		default:
			usage(stderr);
			return 2;
		}
	}

	if (pipe(sig_pipe) < 0) {
		perror(PROG ": pipe");
		return 1;
	}
	/* nonblocking so a signal burst cannot deadlock in the handler */
	fcntl(sig_pipe[0], F_SETFL, O_NONBLOCK);
	fcntl(sig_pipe[1], F_SETFL, O_NONBLOCK);
	fcntl(sig_pipe[0], F_SETFD, FD_CLOEXEC);
	fcntl(sig_pipe[1], F_SETFD, FD_CLOEXEC);

	signal(SIGPIPE, SIG_IGN);

	/* no SA_RESTART: a signal caught inside a blocking read
	 * must abort it instead of resuming the wait */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	pw_init(&argc, &argv);
	pw_state.thread_loop = pw_thread_loop_new(PROG, NULL);
	if (!pw_state.thread_loop) {
		fprintf(stderr, PROG ": cannot create the PipeWire loop\n");
		pw_deinit();
		return 1;
	}
	if (pw_thread_loop_start(pw_state.thread_loop) < 0) {
		fprintf(stderr, PROG ": cannot start the PipeWire loop\n");
		pw_thread_loop_destroy(pw_state.thread_loop);
		pw_deinit();
		return 1;
	}

	pw_thread_loop_lock(pw_state.thread_loop);
	pw_state.context = pw_context_new(
		pw_thread_loop_get_loop(pw_state.thread_loop), NULL, 0);
	if (!pw_state.context) {
		pw_thread_loop_unlock(pw_state.thread_loop);
		fprintf(stderr, PROG ": cannot create the PipeWire context\n");
		pw_thread_loop_stop(pw_state.thread_loop);
		pw_thread_loop_destroy(pw_state.thread_loop);
		pw_deinit();
		return 1;
	}
	pw_state.core = pw_context_connect(pw_state.context, NULL, 0);
	pw_thread_loop_unlock(pw_state.thread_loop);
	if (!pw_state.core) {
		fprintf(stderr, PROG ": cannot connect to the PipeWire daemon, "
			"is it running?\n");
		pw_thread_loop_stop(pw_state.thread_loop);
		pw_context_destroy(pw_state.context);
		pw_thread_loop_destroy(pw_state.thread_loop);
		pw_deinit();
		return 1;
	}

	domain_load();
	if (!g.domain) {
		fprintf(stderr, PROG ": no %s domain, nothing to play\n",
			WMSOUND_DOMAIN);
		return 1;
	}
	if (debug)
		fprintf(stderr, PROG ": loaded %s\n", g.domain_path);

#ifdef HAVE_INOTIFY
	g.inotify_fd = inotify_init1(IN_NONBLOCK);
	if (g.inotify_fd >= 0)
		inotify_watch_update();
#endif

	backoff = RECONNECT_BACKOFF_MS;
	giveup_deadline = monotonic_ms() + CONNECT_GIVEUP_MS;
	for (;;) {
		if (shutdown_pending)
			break;
		path = socket_path ? wstrdup(socket_path) : wIPCSocketPath(display);
		fd = wIPCConnect(path);
		wfree(path);
		stable = 0;
		rc = -1;
		if (fd >= 0) {
			if (wIPCSendStringArray(fd, WIPC_SUBSCRIBE,
						(const char *const[]){"sound"},
						1) >= 0) {
				started = monotonic_ms();
				rc = ipc_loop(fd);
				stable = monotonic_ms() - started
					 >= RECONNECT_STABLE_MS;
			}
			close(fd);
		}
		if (rc == 0 || shutdown_pending)
			break;
		/* A dropped or refused session counts like a failed
		 * connect, only a stable one resets the backoff clock */
		if (stable) {
			backoff = RECONNECT_BACKOFF_MS;
			giveup_deadline = monotonic_ms() + CONNECT_GIVEUP_MS;
		} else if (monotonic_ms() >= giveup_deadline) {
			fprintf(stderr, PROG ": no usable IPC socket after %d min, "
				"giving up\n", CONNECT_GIVEUP_MS / 60000);
			status = 1;
			break;
		}
		if (backoff < RECONNECT_MAX_MS)
			backoff *= 2;
		usleep(backoff * 1000);
	}

	drain_playback();
	pw_thread_loop_lock(pw_state.thread_loop);
	if (pw_state.stream) {
		pw_stream_destroy(pw_state.stream);
		pw_state.stream = NULL;
	}
	if (pw_state.core) {
		pw_core_disconnect(pw_state.core);
		pw_state.core = NULL;
	}
	if (pw_state.context) {
		pw_context_destroy(pw_state.context);
		pw_state.context = NULL;
	}
	pw_thread_loop_unlock(pw_state.thread_loop);
	if (pw_state.samples)
		wfree((void *)pw_state.samples);
	pw_thread_loop_stop(pw_state.thread_loop);
	pw_thread_loop_destroy(pw_state.thread_loop);
	pw_deinit();
	if (g.domain)
		WMReleasePropList(g.domain);
	if (g.domain_path)
		wfree(g.domain_path);
#ifdef HAVE_INOTIFY
	if (g.inotify_wd >= 0)
		inotify_rm_watch(g.inotify_fd, g.inotify_wd);
	if (g.inotify_fd >= 0)
		close(g.inotify_fd);
#endif
	return status;
}
