#!/usr/bin/perl

use strict;

use GD;
use JSON;

my $pngpipe;
open ($pngpipe, "<&STDIN");
binmode($pngpipe);

my $keep_running = 1;
while ($keep_running) {
  my $image = GD::Image->newFromPng($pngpipe);
  if ($image) {

    my $jpeg = unpack "H*", $image->jpeg;

    print to_json {
      "data" => {
          "frame" => {
              "height" => $image->height,
              "width"  => $image->width,
              "index"  => 0,
          },
      },
      "images" => [
          {"hex" => $jpeg},
      ],
    };

    print "\n";
    #$keep_running = 0;
  }
}
