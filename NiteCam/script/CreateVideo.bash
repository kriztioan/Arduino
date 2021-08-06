#!/bin/bash
###
#  @file    main.cpp
#  @brief   Convert JPEGs into Video
#  @author  KrizTioaN (christiaanboersma@hotmail.com)
#  @date    2021-08-06
#  @note    BSD-3 licensed
#
###############################################

for f in  *.jpeg
do
  TIMESTAMP=${f:11:8}
  FILE=${f%.jpeg}.jpg
  [ -f ${FILE} ] || convert -gravity NorthWest -annotate 0 $TIMESTAMP -fill white -pointsize 32 $f ${FILE}
done
convert -quality 100 -delay 8 *.jpg nitecam.mpeg
