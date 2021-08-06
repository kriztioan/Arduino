#!/usr/bin/php
<?php
/**
 * @file   NiteCam.php
 * @brief  Capture Images from Server
 * @author KrizTioaN (christiaanboersma@hotmail.com)
 * @date   2021-08-06
 * @note   BSD-3 licensed
 *
 ***********************************************/

$url = 'http://nitecam.local';

$dir = './';

$delay = 30;

$flag = false;

pcntl_signal(SIGALRM, function ($sig) {

  global $flag;

  $flag = true;
});

pcntl_alarm($delay);

while (true) {

  pcntl_signal_dispatch();

  if ($flag) {

    $time_s = time();

    $now = new DateTime("now", new DateTimeZone('America/Los_Angeles'));

    $dt = new DateInterval('PT12H');

    $now->sub($dt);

    $h = intval($now->format('G'));

    if (
      $h > 7 &&
      $h < 20
    ) { // between 20:00 and 8:00

      if (($fp = @fopen($url, 'r'))) {

        $path = $dir . $now->format('Y-m-d/');

        if (!file_exists($path)) mkdir($path, 0755, true);

        $now->add($dt);

        $path .= $now->format(DATE_W3C) . '.jpeg';

        file_put_contents($path, $fp);
      }
    }

    time() - $time_s;

    $d = $delay - (time() - $time_s);

    if ($d > 0) {

      $flag = false;

      pcntl_alarm($d);
    }
  }

  sleep(1);
}

?>
