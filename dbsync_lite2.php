<?php

require_once 'support_lite.php';

$base_tables = array(
    'COURSE',
    'DOW',
    'INSTRUCTOR',
    'LOCATION',
    'SECTION',
    'TERM',
    'PRODUCT',
    'SALEABLE_ITEM',
    'REQUISITE',
);

$srs_db = srs_data_sync_getSRSConnector();
if (!$srs_db) {
    die("Could not connect to SRS dataase");
}

$mysql = new mysqli($options['db_host'], $options['db_user'], $options['db_passwd'], $options['db_name'], $options['db_port']);
if ($mysql->connect_error) {
    die('Connect Error (' . $mysql->connect_errno . ') ' . $mysql->connect_error);
}
if (!$mysql->autocommit(FALSE)) {
    die('Autocommit error: ' . $mysql->error);
}

$tables =& $ORA_TABLES;

foreach ($tables as $table) {

    $max_audit_id_result = $mysql->query(sprintf('SELECT max(`AUD_ID`) FROM `%s`;', $table['name']), MYSQLI_USE_RESULT);
    if (! $max_audit_id_result) {
        error_log("Couldn't select max(AUD_ID) for table " . $table['name']);
        $mysql->rollback();
        break;
    }
    $max_audit_id_row = $max_audit_id_result->fetch_row();
    $max_audit_id = $max_audit_id_row[0];
    $max_audit_id_result->free();

		$srs_check_st = oci_parse($srs_db, sprintf('SELECT AUD_ID FROM "%s" WHERE :max IS NULL OR AUD_ID > :max', $table['name']));
    if (!oci_bind_by_name($srs_check_st, ':max', $max_audit_id)) {
        error_log("Couldn't bind SELECT for " . $table['name']);
        error_log("Table definition is " . $table['create_table_stmt']);
        $mysql->rollback();
        break;
    }

    if (!oci_execute($srs_check_st)) {
        error_log("Couldn't check new rows for table " . $table['name']);
        $mysql->rollback();
        break;
    }
    
    $checkrow = oci_fetch_array($srs_check_st, OCI_NUM + OCI_RETURN_NULLS);
    if(!isset($checkrow[0])) {
			//No new rows so don't run the subsequent expensive queries
	    continue;
    }

    $srs_select_st = oci_parse($srs_db, sprintf('SELECT * FROM "%s" WHERE :max IS NULL OR AUD_ID > :max', $table['name']));
        
    if (!oci_bind_by_name($srs_select_st, ':max', $max_audit_id)) {
        error_log("Couldn't bind SELECT for " . $table['name']);
        error_log("Table definition is " . $table['create_table_stmt']);
        $mysql->rollback();
        break;
    }

    if (!oci_execute($srs_select_st)) {
        error_log("Couldn't select new rows for table " . $table['name']);
        $mysql->rollback();
        break;
    }

    $insert_st = $mysql->prepare(sprintf('INSERT IGNORE INTO `%s` (%s) VALUES (%s);', $table['name'], $table['col_names'], $table['col_placeholders']));
    if (!$insert_st) {
        die('Prepare INSERT error: ' . $mysql->error);
    }

    $i = 0;
    while ($row = oci_fetch_array($srs_select_st, OCI_NUM + OCI_RETURN_NULLS)) {

        $is_php53_or_later = (strnatcmp(phpversion(),'5.3') >= 0);
        $null = NULL;
        $bind_params = array($insert_st, $table['mysqli_placeholder_types']);
        for ($c = 0; $c < strlen($table['mysqli_placeholder_types']); $c++) {
            if ($table['mysqli_placeholder_types'][$c] == 'b' && !is_null($row[$c])) {
                # will call send_long_data() later
                if ($is_php53_or_later) {
                    $bind_params[] = &$null;
                } else {
                    $bind_params[] = NULL;
                }
            } else {
                if ($is_php53_or_later) {
                    $bind_params[] = &$row[$c];
                } else {
                    $bind_params[] = $row[$c];
                }
            }
        }

        call_user_func_array('mysqli_stmt_bind_param', $bind_params);

        # send_long_data() must be called after bind_param()
        for ($c = 0; $c < strlen($table['mysqli_placeholder_types']); $c++) {
            if ($table['mysqli_placeholder_types'][$c] == 'b' && !is_null($row[$c])) {
                if (!$insert_st->send_long_data($c, $row[$c]->load())) {
                    die('send_long_data() failed: ' . $insert_st->error);
                }
            }
        }

        $insert_st->execute();
        # enable for output: progress_bar(++$i, 100, $table['name'] . ': ');
    }

    if (!$mysql->commit()) {
        error_log('COMMIT error for table ' . $table['name'] . ': ' . $mysql->error);
    }
}

$mysql->close();
oci_close($srs_db);
