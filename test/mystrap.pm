# vim:ts=2:sw=2:et:
# Copyright (c) 2008 Message Systems, Inc.
package test::mystrap;
use strict;
use Test::Harness::Straps;

our @ISA = qw(Test::Harness::Straps);

sub _command_line {
  my ($self, $file) = @_;
  if ($file =~ m/\.lua$/) {
    return $ENV{RCLUA_EXECUTABLE} . " $file";
  }
  return $self->SUPER::_command_line($file);
}

1;
