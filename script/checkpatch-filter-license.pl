#!/usr/bin/perl
#
# Drop per-file diff sections for license files before checkpatch linting.
# License texts are verbatim documents whose wording (FSF mailing address)
# and formatting (form feeds) are not ours to change, so the kernel-oriented
# style checks do not apply to them.
#
use strict;
use warnings;

my $skip = 0;

while (my $line = <STDIN>) {
	if ($line =~ m{^diff --git a/\S+ b/(\S+)}) {
		my $name = $1;
		$name =~ s{.*/}{};
		$skip = $name =~ /^(?:copying|licen[cs]e)/i;
	}
	print $line unless $skip;
}

exit 0;
