# vim:ts=2:sw=2:et:
# Copyright (c) 2013 Message Systems, Inc.

package t::TAP_Harness;
use strict;
use warnings;
use TAP::Harness;
use Carp qw/cluck/;

our @ISA = qw(TAP::Harness);

sub new {
  my $self = shift;
  my $opts = shift;

  if (!defined $opts->{exec}) {
    $opts->{exec} = \&_exec;
  } else {
    cluck "exec option already passed to TAP::Harness->new()";
  }
  return $self->SUPER::new($opts, @_);
}

sub _exec {
  my ($harness, $file) = @_;
  my @cmd;

  if ($file =~ m/\.lua$/) {
    if ($ENV{USE_VALGRIND}) {
      my $logname = $file . ".vglog";
      my $dsymutl = ($^O eq 'darwin') ? ' --dsymutil=yes ' : '';

      push(@cmd, "/opt/msys/3rdParty/bin/valgrind", "--tool=memcheck");
      if ($dsymutl ne '') {
        push(@cmd, $dsymutl);
      }
      push(@cmd,
        "--log-file=$logname",
        "--leak-check=full", "--track-origins=yes", "--show-reachable=yes");
    }
    push(@cmd, $ENV{LUA_EXECUTABLE});
  } elsif ($file =~ m/\.luat$/) {
    push(@cmd, $^X, "t/doluat");
  }

  # XXX: How to support Lua coverage here (see t::strap)?

  if ($#cmd >= 0) {
    return [ @cmd, $file ];
  }

  # Fall back to default TAP::Harness behaviour.
  return undef;
}

1;

