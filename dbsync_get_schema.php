<?php

require_once 'support_lite.php';

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

$table_string = print_r($tables, true);
$date = date('Y-m-d');
file_put_contents('schema-'.$date.'.txt', $table_string);

$mysql->close();
oci_close($srs_db);
