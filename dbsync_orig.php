<?php

require_once 'support_orig.php';

function get_srs_schema($srs_db, $owner, $base_tables) {
    foreach ($base_tables as $k => $table) {
        $base_tables[$k] = "'$table'";
    }

    $schema_scraping_query = "
    WITH interest(name) AS (
        SELECT column_value AS FROM XMLTABLE(:base_tables)
    ), possible_joins(a_name, b_name) AS (
        SELECT a.name, b.name FROM interest a CROSS JOIN interest b
    ), audit_tables(table_name) AS (
        SELECT 'AUD_' || a_name || '_' || b_name
            FROM possible_joins
      UNION
        SELECT 'AUD_' || name
            FROM interest
    ),

    constraints AS (
        SELECT cols.*, cons.constraint_type
            FROM all_constraints cons
                INNER JOIN all_cons_columns cols
                    ON cols.constraint_name = cons.constraint_name
                    AND cols.owner = cons.owner
            WHERE
                cons.status = 'ENABLED'
                AND cons.constraint_type IN ('P', 'U')
            ORDER BY cons.table_name, cols.position
    ), audit_schema AS (
        SELECT *
            FROM all_tab_cols
    ), audit_schema_mysql_types AS (
        SELECT audit_schema.*
             , CASE data_type
                 WHEN 'NUMBER'       THEN 'NUMERIC(' || COALESCE(data_precision, 38) || ',' || COALESCE(data_scale, 0) || ')'
                 WHEN 'VARCHAR2'     THEN 'VARCHAR(' || data_length || ')'
                 WHEN 'CHAR'         THEN 'CHAR(' || data_length || ')'
                 WHEN 'DATE'         THEN 'VARCHAR(10)'
                 WHEN 'CLOB'         THEN 'BLOB'
                 WHEN 'TIMESTAMP(6)' THEN 'TIMESTAMP'
                 ELSE                data_type
               END
               ||
               CASE nullable
                 WHEN 'N'            THEN ' NOT NULL'
               END AS mysql_type
            , CASE data_type
                WHEN 'BLOB'          THEN 'b'
                WHEN 'CLOB'          THEN 'b'
                ELSE                      's'
              END AS mysqli_placeholder_type
            FROM audit_schema
    ), mysql_constraint_defs AS (
        SELECT owner
             , table_name
             , constraint_type
             , constraint_name
             , ', CONSTRAINT ' || constraint_name
               || CASE constraint_type
                     WHEN 'P' THEN ' PRIMARY KEY'
                     WHEN 'U' THEN ' UNIQUE'
                  END
               || ' ('
               ||    LISTAGG('`' || column_name || '`', ', ') WITHIN GROUP (ORDER BY position)
               || ')'
               || CHR(10) AS constraint_def
            FROM constraints
            GROUP BY owner, table_name, constraint_type, constraint_name
    ), mysql_col_defs AS (
        SELECT owner
             , table_name
             -- , LISTAGG('`' || column_name || '`', ', ') WITHIN GROUP (ORDER BY column_id) AS col_names
             , RTRIM(EXTRACT(XMLAGG(XMLELEMENT(e, '`' || column_name || '`' || ', ') ORDER BY column_id),
                             '/E/text()').getCLOBVal(), ', ') AS col_names
             , LISTAGG('?', ', ') WITHIN GROUP (ORDER BY column_id) AS col_placeholders
             , LISTAGG(mysqli_placeholder_type) WITHIN GROUP (ORDER BY column_id) AS mysqli_placeholder_types
             --, LISTAGG('`' || column_name || '`' || ' ' || mysql_type, CHR(10) || ', ') WITHIN GROUP (ORDER BY column_id) || CHR(10) AS col_defs
             , RTRIM(EXTRACT(XMLAGG(XMLELEMENT(e, '`' || column_name || '`' || ' ' || mysql_type, CHR(10) || ', ') ORDER BY column_id),
                             '/E/text()').getCLOBVal(), ', ') AS col_defs
            FROM audit_schema_mysql_types
            GROUP BY owner, table_name
    ), mysql_table_constraints AS (
        SELECT owner
             , table_name
             , LISTAGG(constraint_def, CHR(10)) WITHIN GROUP (ORDER BY constraint_type, constraint_name) AS constraints
            FROM mysql_constraint_defs
            GROUP BY owner, table_name
    ), mysql_defs AS (
        SELECT mysql_col_defs.owner
             , mysql_col_defs.table_name
             , mysql_col_defs.col_names
             , mysql_col_defs.col_placeholders
             , mysql_col_defs.mysqli_placeholder_types
             , mysql_col_defs.col_defs
             , mysql_table_constraints.constraints
            FROM mysql_col_defs
                LEFT OUTER JOIN mysql_table_constraints
                    ON mysql_table_constraints.owner = mysql_col_defs.owner
                    AND mysql_table_constraints.table_name = mysql_col_defs.table_name
    )
    SELECT table_name
        ,  col_names
        ,  col_placeholders
        ,  mysqli_placeholder_types
        , 'CREATE TABLE IF NOT EXISTS `' || table_name || '`' || CHR(10)
        || '( '
        ||    col_defs
        ||    constraints
        || ') ENGINE=INNODB;' AS create_table_stmt
        FROM mysql_defs
        WHERE
            owner = :owner
            AND table_name IN (SELECT table_name FROM audit_tables)
        ORDER BY
            owner,
            table_name
    ";


    $schema_st = oci_parse($srs_db, $schema_scraping_query);
    if ( !oci_bind_by_name($schema_st, ':owner', $owner) ||
         !oci_bind_by_name($schema_st, ':base_tables', implode(',', $base_tables)) ||
         !oci_execute($schema_st) ) {
        $err = oci_error($schema_st);
        error_log("Could not detect SRS database schema: " . $err['message']);
        error_log($schema_scraping_query);
        return;
    }

    while ($row = oci_fetch_assoc($schema_st)) {
        $tables[$row['TABLE_NAME']] = array(
            'name'                      => $row['TABLE_NAME'],
            'col_names'                 => $row['COL_NAMES']->load(),
            'col_placeholders'          => $row['COL_PLACEHOLDERS'],
            'mysqli_placeholder_types'  => $row['MYSQLI_PLACEHOLDER_TYPES'],
            'create_table_stmt'         => $row['CREATE_TABLE_STMT']->load(),
        );
    }
    oci_free_statement($schema_st);

    return $tables;
}


function create_mysql_schema($mysql, $tables, $wanted_table_name=NULL) {
    foreach ($tables as $table) {
        if (is_null($wanted_table_name) || $wanted_table_name == $table['name']) {
            //error_log($table['create_table_stmt']); # TODO: remove debug stmt
            //if (!$mysql->query(sprintf("TRUNCATE `%s`;", $table['name']))) {
            //    error_log('Failed to truncate ' . $table['name'] . ': ' . $mysql->error);
            //    return FALSE;
            //}
            if (!$mysql->query($table['create_table_stmt'])) {
                error_log('Failed to execute ' . $table['create_table_stmt'] . ': ' . $mysql->error);
                return FALSE;
            }
        }
    }
    return TRUE;
}


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

if (! ($tables = get_srs_schema($srs_db, 'REGSYSDB', $base_tables))) {
    die("Could not get SRS schema");
}

foreach ($tables as $table) {
    if (! create_mysql_schema($mysql, $tables, $table['name'])) {
        die("Could not create MySQL schema");
    }
    if (! $mysql->query(sprintf('ALTER TABLE `%s` ADD COLUMN drupal_import_time DATETIME;', $table['name']))
          && $mysql->error != "Duplicate column name 'drupal_import_time'" ) {
        die("Could not add timstamp column to table " . $table['name'] . ": " . $mysql->error);
    }

    $max_audit_id_result = $mysql->query(sprintf('SELECT max(`AUD_ID`) FROM `%s`;', $table['name']), MYSQLI_USE_RESULT);
    if (! $max_audit_id_result) {
        error_log("Couldn't select max(AUD_ID) for table " . $table['name']);
        $mysql->rollback();
        break;
    }
    $max_audit_id_row = $max_audit_id_result->fetch_row();
    $max_audit_id = $max_audit_id_row[0];
    $max_audit_id_result->free();

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
        # BLOBs need a separate call to be loaded.  Also, in PHP >= 5.3,
        # mysqli_stmt_bind_param() requires parameters to be references
        # http://php.net/manual/en/mysqli-stmt.bind-param.php#96770
        # http://php.net/manual/en/mysqli-stmt.bind-param.php#89171
        #
        # This is the intent, but it is insufficient...
        #$insert_st->bind_param($table['mysqli_placeholder_types'], $row...);

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
        progress_bar(++$i, 100, $table['name'] . ': ');
    }

    if (!$mysql->commit()) {
        error_log('COMMIT error for table ' . $table['name'] . ': ' . $mysql->error);
    }
}

$mysql->close();
oci_close($srs_db);
