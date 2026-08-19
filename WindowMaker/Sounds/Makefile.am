sounddatadir = $(pkgdatadir)/Sounds

# No sound files ship, but the directory is advertised in the default
# SoundPath, so create it on install
sounddata_DATA =

EXTRA_DIST = $(sounddata_DATA)

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(sounddatadir)
