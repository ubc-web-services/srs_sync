<?php
#uncomment the return statement to shut this off!
#return;


$time_start = microtime(true);

$dr_sync = 'cd {path/to/siteroot}; drush srs_data_sync-import;';

# Run drush script to sync audit rows with Drupal DB
exec($dr_sync); 

$time_end = microtime(true);
$time = $time_end - $time_start;

# If total script time is under 20 seconds, run it again
if($time < 20) {
	sleep(8);	
	exec($dr_sync); 
	sleep(8);	
}

$time_end = microtime(true);
$time = $time_end - $time_start;

# If total script time is under 35 seconds, run it again
while($time < 35) {
	exec($dr_sync); 
	sleep(8);	

	$time_end = microtime(true);
	$time = $time_end - $time_start;
}