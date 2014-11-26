<?php
#uncomment the return statement to shut this off!
#return;


$time_start = microtime(true);

$ora_sync = 'php /www_data/aegir/platforms/drupal-7.21-mar-15-2013/sites/cstudies.ubc.ca/sync/dbsync_lite2.php';

# run audit row sync with SRS DB
exec($ora_sync);

$time_end = microtime(true);
$time = $time_end - $time_start;

# If total script time is under 20 seconds, run it again
if($time < 20) {
  sleep(5);
	exec($ora_sync); 
}

$time_end = microtime(true);
$time = $time_end - $time_start;

# If total script time is under 40 seconds, run it again
if($time < 40) {
	exec($ora_sync); 
}