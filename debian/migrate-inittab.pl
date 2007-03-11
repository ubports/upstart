#!/usr/bin/perl

use strict;
use warnings;

my %gettys;
my $have_cad = 0;


#-----------------------------------------------------------------------------#
# Parse /etc/inittab
#-----------------------------------------------------------------------------#

open INITTAB, "/etc/inittab"
    or die "Unable to open /etc/inittab: $!";

while (<INITTAB>) {
    chomp;
    s/^\s*//;

    next if /^\#/;
    next unless length;

    my ($id, $rlevel, $action, $process) = split /:/, $_, 4;

    warn "missing id field" and next
	unless defined $id and length $id;
    warn "missing runlevel field" and next
	unless defined $rlevel;
    warn "missing action field" and next
	unless defined $action and length $action;
    warn "missing process field" and next
	unless defined $process;


    $have_cad = 1 if $action eq "ctrlaltdel";
    $gettys{$1} = [ $rlevel, $process ]	if $process =~ /getty.*\b(tty\w+)/;
}

close INITTAB
    or warn "Error while closing /etc/inittab: $!";


#-----------------------------------------------------------------------------#
# Alter /etc/event.d
#-----------------------------------------------------------------------------#

unlink "/etc/event.d/control-alt-delete"
    unless $have_cad;

foreach (qw/tty1 tty2 tty3 tty4 tty5 tty6/) {
    unlink "/etc/event.d/$_"
	unless exists $gettys{$_};
}

foreach (sort keys %gettys) {
    my ($rlevel, $process) = @{$gettys{$_}};

    my @job;
    if (-f "/etc/event.d/$_") {
	open JOB, "/etc/event.d/$_"
	    or warn "Unable to open /etc/event.d/$_: $!" and next;
	@job = <JOB>;
	close JOB
	    or warn "Error while closing /etc/event,d/$_: $!" and next;

	foreach my $rl (qw/2 3 4 5/) {
	    my $idx;
	    for ($idx = 0; $idx < @job; $idx++) {
		last if $job[$idx] =~ /^\s*(start|stop)\s+on\s+runlevel\s+$rl\b/;
	    }

	    if ($idx < @job) {
		if ($rlevel =~ /$rl/) {
		    $job[$idx] =~ s/^(\s*)stop(\s+)/$1start$2/;
		} else {
		    $job[$idx] =~ s/^(\s*)start(\s+)/$1stop$2/;
		}
	    } else {
		if ($rlevel =~ /$rl/) {
		    push @job, "start on runlevel $rl\n";
		} else {
		    push @job, "stop on runlevel $rl\n";
		}
	    }
	}

	my $idx;
	for ($idx = 0; $idx < @job; $idx++) {
	    last if $job[$idx] =~ /^\s*respawn\s+/;
	}

	if ($idx < @job) {
	    $job[$idx] =~ s/^(\s*respawn\s+).*/$1$process/;
	} else {
	    push @job, "respawn\n";
	    push @job, "exec $process\n";
	}

    } else {
	push @job, "# $_ - getty\n";
	push @job, "#\n";
	push @job, "# Converted from /etc/inittab entry\n";
	push @job, "\n";

	foreach my $rl (qw/2 3 4 5/) {
	    if ($rlevel =~ /$rl/) {
		push @job, "start on runlevel $rl\n";
	    } else {
		push @job, "stop on runlevel $rl\n";
	    }
	}
	push @job, "\n";

	push @job, "stop on shutdown\n";
	push @job, "\n";

	push @job, "respawn\n";
	push @job, "exec $process\n";
    }

    open JOB, ">/etc/event.d/.$_"
	or warn "Unable to write to /etc/event.d/.$_: $!" and next;
    print JOB @job;
    unless (close JOB) {
	warn "Error while closing /etc/event.d/.$_: $!";
	unlink "/etc/event.d/.$_";
	next;
    }

    unless (rename "/etc/event.d/.$_", "/etc/event.d/$_") {
	warn "Unable to replace /etc/event.d/$_: $!";
	unlink "/etc/event.d/.$_";
	next;
    }
}
