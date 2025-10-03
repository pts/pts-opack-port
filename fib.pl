#!/usr/bin/env perl

$a = $b = 1;

map { print chr($_) x $a; $c = $b; $b += $a; $a = $c; } (32..64);


