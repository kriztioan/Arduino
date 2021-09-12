<?php

/**
 * @file   SensorPod.php
 * @brief  Store and Show Sensor Pod Readings
 * @author KrizTioaN (christiaanboersma@hotmail.com)
 * @date   2021-08-06
 * @note   BSD-3 licensed
 *
 ***********************************************/

if (
  $_SERVER['REQUEST_METHOD'] == 'POST' ||
  ($_SERVER['REQUEST_METHOD'] == 'GET' && isset($_GET['dt']))
) {

  $link = mysqli_connect("localhost", "arduino", "sketch", "sensors");
  if (mysqli_connect_errno()) {
    echo "Failed to connect to MySQL: " . mysqli_connect_error();
    http_response_code(500);
    exit();
  }

  header('Content-Type: application/json');

  if ($_SERVER['REQUEST_METHOD'] == 'POST') {
    $json = file_get_contents('php://input');
    $d = json_decode($json);
    $query = "INSERT INTO readings(timestamp, temperature, humidity, pressure, photo, pm10, pm25) VALUES (?, ?, ?, ?, ?, ?, ?)";
    $st = mysqli_prepare($link, $query);
    mysqli_stmt_bind_param($st, 'sdddddd', $d->timestamp, $d->temperature, $d->humidity, $d->pressure, $d->photo, $d->pm10, $d->pm25);
    if (!mysqli_stmt_execute($st)) http_response_code(400);
    else http_response_code(200);
    mysqli_close($link);
    exit();
  }

  $data = array(
    'cols' => array(
      (object) array('label' => 'timestamp', 'type' => 'date'),
      (object) array('label' => 'temperature', 'type' => 'number'),
      (object) array('label' => 'humidity', 'type' => 'number'),
      (object) array('label' => 'pressure', 'type' => 'number'),
      (object) array('label' => 'photo', 'type' => 'number'),
      (object) array('label' => 'pm10', 'type' => 'number'),
      (object) array('label' => 'pm25', 'type' => 'number')
    ),
    'rows' => array()
  );

  $where = '';
  if (!empty($_GET['dt'])) $where = " WHERE timestamp > '" . $_GET['dt'] . "'";
  $query = "SELECT DATE_FORMAT(timestamp, '%Y-%m-%dT%H:%i:%s') AS timestamp, temperature, humidity, pressure, LOG(photo) as photo, pm10, pm25 FROM readings" . $where;

  $result = mysqli_query($link, $query);
  while ($row = mysqli_fetch_assoc($result)) {
    $tm = localtime(strtotime($row['timestamp']), true);
    $tm = array(1900 + $tm['tm_year'], $tm['tm_mon'], $tm['tm_mday'], $tm['tm_hour'], $tm['tm_min'], $tm['tm_sec']);
    $data['rows'][] = (object) array('c' => array(
      (object) array('v' => 'Date(' . join(',', $tm) . ')'),
      (object) array('v' => floatval($row['temperature'])),
      (object) array('v' => floatval($row['humidity'])),
      (object) array('v' => (floatval($row['pressure']) - 1.0) * 1000),
      (object) array('v' => floatval($row['photo'])),
      (object) array('v' => floatval($row['pm10'])),
      (object) array('v' => floatval($row['pm25']))
    ));
  }

  mysqli_free_result($result);
  mysqli_close($link);

  echo json_encode((object) $data);
  exit();
}
?>
<!DOCTYPE html>
<html>

<head>
  <title>Sensors</title>
  <script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script>
  <script type="text/javascript" src="https://ajax.googleapis.com/ajax/libs/jquery/1.10.2/jquery.min.js"></script>
  <script type="text/javascript">
    google.charts.load('current', {
      'packages': ['corechart', 'controls']
    });
    google.charts.setOnLoadCallback(draw);

    var data = null;

    var board = null;

    var opt = {
      title: 'Sensors',
      curveType: 'none',
      legend: {
        position: 'right'
      },
      hAxis: {
        title: 'timestamp [date-time]',
      },
      vAxis: {
        title: 'measurement [% | log lx | \u2103 | ug/m\u00B3 | mbar]',
      },
      explorer: {
        actions: ['dragToZoom', 'rightClickToReset'],
        axis: 'horizontal',
        keepInBounds: true,
        maxZoomIn: 0.0
      },
    };

    function draw() {

      $.ajax({
        url: window.location.href + "?dt=",
        dataType: "json",
        success(json) {
          data = new google.visualization.DataTable(json);
          dash = document.getElementById('container')
          board = new google.visualization.Dashboard(dash);
          endDate = data.getValue(data.getNumberOfRows() - 1, 0)
          startDate = new Date(endDate);
          startDate.setDate(endDate.getDate() - 2);
          slider = new google.visualization.ControlWrapper({
            'controlType': 'ChartRangeFilter',
            'containerId': 'control',
            'options': {
              'filterColumnLabel': 'timestamp',
              'ui': {
                'chartType': 'LineChart',
                'chartOptions': {
                  'chartArea': {
                    'width': '70%'
                  },
                  'hAxis': {
                    'baselineColor': 'none'
                  },
                },
                'minRangeSize': 86400000,
              },
            },
            'state': {
              'range': {
                'start': startDate,
                'end': endDate
              }
            }
          });
          line = new google.visualization.ChartWrapper({
            'chartType': 'LineChart',
            'options': opt,
            'containerId': 'line',
          });

          board.bind(slider, line);
          board.draw(data);
        }
      });
    }


    function update() {

      var dt = data.getValue(data.getNumberOfRows() - 1, 0);

      var json = $.ajax({
        url: window.location.href + "?dt=" + dt.getFullYear() + '-' + (dt.getMonth() + 1) + '-' + dt.getDate() + 'T' + dt.getHours() + ':' + dt.getMinutes() + ':' + dt.getSeconds(),
        dataType: "json",
        success(json) {
          var d = new google.visualization.DataTable(json);
          data = google.visualization.data.join(data, d, 'full', [
            [0, 0],
            [1, 1],
            [2, 2],
            [3, 3],
            [4, 4],
            [5, 5],
            [6, 6]
          ], [], []);
          board.draw(data);
        }
      });
    }

    $(window).resize(function() {
      if (this.resizeTO) clearTimeout(this.resizeTO);
      this.resizeTO = setTimeout(function() {
        $(this).trigger('resizeEnd');
      }, 500);
    });

    $(window).on('resizeEnd', function() {
      board.draw(data);
    });

    $(function() {
      setInterval(update, 300000);
    });
  </script>
</head>

<body>

  <div id="dash">
    <div id="line" style="width: 98vw; height: 80vh;"></div>
    <div id="control" style="width: 98vw; height: 12vh;"></div>
  </div>
</body>

</html>
