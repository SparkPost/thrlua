# vim:ts=2:sw=2:et:
# Copyright (c) 2008-2010 Message Systems, Inc.
package t::strap;
use strict;
use Test::Harness::Straps;

our @ISA = qw(Test::Harness::Straps);

sub _command_line {
  my ($self, $file) = @_;
  my $cmd;

  if ($file =~ m/\.lua$/) {
    $cmd = $ENV{LUA_EXECUTABLE} . " $file";
    if ($ENV{USE_VALGRIND}) {
      my $logname = $file . ".vglog";
      my $dsymutl = ($^O eq 'darwin') ? ' --dsymutil=yes ' : '';
      $cmd = "valgrind --tool=memcheck $dsymutl --log-file=$logname --leak-check=full --track-origins=yes --show-reachable=yes $cmd";
    }
  } elsif ($file =~ m/\.luat$/) {
    $cmd = $^X . " t/doluat $file";
  } else {
    $cmd = $self->SUPER::_command_line($file);
  }

  if (exists $ENV{LUA_LCOV}) {
    my $lcov = $ENV{LUA_LCOV};
    my $infoname = $file . ".lcovinfo";
    my $testname = $file;
    $testname =~ s/[^a-zA-Z0-9_]/_/g;

    $cmd = "$lcov -q --zerocounters --directory .; $cmd ; $lcov --capture --base-directory . --directory . --output-file $infoname --test-name $testname";
  }
  return $cmd;
}

1;
