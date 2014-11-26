<?php

$options['db_type'] = '';
$options['db_host'] = '';
$options['db_port'] = '';
$options['db_passwd'] = '';
$options['db_name'] = '';
$options['db_user'] = '';


define('ORACLE_HOST', '');
define('ORACLE_PORT', '');
define('ORACLE_SID', '');
define('ORACLE_GLOBAL_NAME', '');
define('ORACLE_USER', '');
define('ORACLE_PWORD', '');
define('UBC_SRS_ORACLE_CONN', '');

function srs_data_sync_getSRSConnector() {

  srs_data_sync_logTransaction('START', '(SRS CONNECT)');

  $tnsName = '(DESCRIPTION =
                            (ADDRESS =
                              (PROTOCOL = TCP)
                              (Host = '.ORACLE_HOST.')
                              (Port = '.ORACLE_PORT.')
                                )
                            (CONNECT_DATA =
                          (SID = '.ORACLE_SID.')
                          (GLOBAL_NAME = '.ORACLE_GLOBAL_NAME.')
                            )
                          )';
  
  $conn = oci_connect(ORACLE_USER, ORACLE_PWORD, $tnsName);

  if (!$conn) {
    $e = oci_error();
    //watchdog('Oracle Test', 'Set Connection: Failed to connect to Oracle', NULL, WATCHDOG_NOTICE);
    srs_data_sync_logTransaction('FAILED', '(SRS CONNECT)');
    return FALSE;
  }
  else {
    if(UBC_SRS_ORACLE_CONN == 'PROD') {
          $query = oci_parse($conn, "ALTER SESSION SET CURRENT_SCHEMA=REGSYSDB");
          oci_execute($query);
      srs_data_sync_logTransaction('ALTER DB SESSION - PROD', '(SRS CONNECT)');
    }

    srs_data_sync_logTransaction('SUCCESS', '(SRS CONNECT)');
    return $conn;
  }  
}

function srs_data_sync_logTransaction($a, $b) {
  echo $a, $b, "\n";
}

function refValues($arr) { 
  # In PHP >= 5.3, mysqli_stmt_bind_param() requires parameters to be references
  # http://php.net/manual/en/mysqli-stmt.bind-param.php#96770
  if (strnatcmp(phpversion(),'5.3') >= 0) {
    $refs = array(); 
    foreach ($arr as $key => $value) {
      $refs[$key] = &$arr[$key]; 
    }
    return $refs; 
  } 
  return $arr; 
}

function progress_bar($numerator, $divs, $prefix='', $suffix='') {
    $prev_proportion = ($numerator - 1) / $divs;
    $proportion      = ($numerator    ) / $divs;
    if (floor($prev_proportion) != floor($proportion)) {
        printf("%s%5d%s\n", $prefix, $numerator, $suffix);
    }
}
