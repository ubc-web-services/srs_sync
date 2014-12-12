<?php

define('INSTRUCTOR_SECTION_TBA_ID', 1461);
define('PROCESS_BUNDLES', TRUE);
define('PROCESS_RELATIONSHIPS', TRUE);
define('PROCESS_REQUISITES', TRUE);
define('PROCESS_FEES', TRUE);
define('LIMIT_RESULTS', ''); //leave preceding space :: ' LIMIT X'
define('ECHO_ERRORS', FALSE);
define('ALERT_FILE', '{path/to/alert/file}');

function srs_data_sync_drush_command() {
  $items['srs_data_sync-import'] = array(
    'description' => dt('Import data to Drupal'),
  );
  return $items;
}

function srs_data_sync_alertError($audit_row, $type, $msg=false) {
	$msg = 'Failed running AUDIT_ROW: '.$audit_row.' :: TABLE: '.$type;
	if($msg) $msg .= ' :: MSG: '.$msg;


	$i = file_get_contents(ALERT_FILE);
  $errs = explode("/", $i);	
	$hour = date('G');
	
	if(intval($errs[0]) == intval($hour)) {
	  $errnum = $errs[1];
	  if(($errnum > 10) && ($errnum <= 20)) {
	    mail('{your@email.addr}', 'Drush Synch Did Not Complete', $msg);
	  }
	  
	  $errnum = $errnum + 1;
		$log = $hour.'/'.$errnum;
		file_put_contents(ALERT_FILE, $log);
	}
	else {
		$log = $hour.'/1';
		file_put_contents(ALERT_FILE, $log);
	}
}

function srs_data_sync_logOrEcho($msg) {
  if(ECHO_ERRORS) echo $msg."\n";
  else srs_data_sync_logTransaction($msg, 'DRUSH SYNC');
}

function drush_srs_data_sync_import() {

  if(!extension_loaded('oci8')) {
		srs_data_sync_logOrEcho('OCI8 NOT LOADED');
    return;
  }
	
  module_load_include('inc', 'srs_data_sync', 'srs_data_sync.map');

  db_query("SET SESSION TRANSACTION ISOLATION LEVEL SERIALIZABLE;");
  $xact = db_transaction();
	srs_data_sync_logOrEcho('*Drupal Synch Begin*');

  if(PROCESS_BUNDLES) {
  	if(!srs_data_sync_importBundles($xact)) {
	    $xact->rollback();
	    srs_data_sync_logOrEcho('Rolled back after bundles');
	    return;
    }
  }
  if(PROCESS_RELATIONSHIPS) {
  	if(!srs_data_sync_importRelationships($xact)) {
	    $xact->rollback();
	    srs_data_sync_logOrEcho('Rolled back after relationships');
	    return;
		}
  }
  if(PROCESS_REQUISITES) {
    if(!srs_data_sync_importRequisiteRelationships($xact)) {
	    $xact->rollback();
	    srs_data_sync_logOrEcho('Rolled back after requisites');
	    return;
		}    
  }
  if(PROCESS_FEES) {
  	if(!srs_data_sync_importFees($xact)) {
	    $xact->rollback();
	    srs_data_sync_logOrEcho('Rolled back after fees');
	    return;
		}
  }
  
  srs_data_sync_logOrEcho('*Drupal Synch Complete*');
  srs_data_sync_logOrEcho('');
  return;

}

function srs_data_sync_rollbackAuditAction($xact, $audrow, $table, $msg=false) {
  $xact->rollback();
  srs_data_sync_alertError($audrow->AUD_ID, $table, $msg);

  srs_data_sync_logOrEcho('Error running AUD_ID: '.$audrow->AUD_ID.'  Table: '.$table);
  srs_data_sync_logOrEcho('Rolled back');
  return false;
}

function srs_data_sync_timestampAuditRow($audrow, $table) {
  srs_data_sync_logOrEcho('Timestamp AUD_ID: '.$audrow->AUD_ID);
  db_query(sprintf("UPDATE %s SET drupal_import_time = NOW() WHERE AUD_ID=%d", $table, $audrow->AUD_ID));
}






function srs_data_sync_importFees($xact) {

  srs_data_sync_logOrEcho('RUNNING FEES');

	if($audrows = db_query(sprintf("SELECT * FROM %s WHERE drupal_import_time IS NULL".LIMIT_RESULTS." FOR UPDATE;", 'AUD_SALEABLE_ITEM'))) {
	  $sections = array();
	  $products = array();

		$in = '';	
		$parents = '';
		$cmp = array();
		
		foreach($audrows as $i => $audrow) {
		  $in .= $audrow->ITEM_ID.',';
		  $parents .= $audrow->SALEABLE_ITEM_PARENT_ID.',';
		  $cmp[] = $audrow->AUD_ID;
		  srs_data_sync_logOrEcho('RUN AUD_ID: '.$audrow->AUD_ID);
		}
		$in = trim($in, ',');
		$parents = trim($parents, ',');
		if($in == '') {
		  //srs_data_sync_logOrEcho('NO FEES TO RUN');
		  return true;
		}		
		
	  $conn = srs_data_sync_getSRSConnector();
		if(!$conn) {
		  $xact->rollback();
		  srs_data_sync_alertError('connector', 'fees');

		  srs_data_sync_logOrEcho('Error acquiring SRS Connector');
		  srs_data_sync_logOrEcho('Rolled back');
		  return false;
    }

		//add items from parent ids	
	  $p_query = oci_parse($conn, "SELECT ITEM_ID FROM SALEABLE_ITEM WHERE SALEABLE_ITEM_ID IN(".$parents.")");
		oci_execute($p_query);
	  while($p_row = oci_fetch_array($p_query, OCI_ASSOC+OCI_RETURN_NULLS)) {
			$in .= ', '.$p_row['ITEM_ID'];
	  }
	
		//get products and sections to run updates on
		$query = oci_parse($conn, "SELECT I.ITEM_ID, I.PRODUCT_ID, I.SECTION_COURSE_ID, S.SECTION_ID 
																FROM ITEM I 
																LEFT OUTER JOIN SECTION_COURSE S ON S.SECTION_COURSE_ID = I.SECTION_COURSE_ID 
																WHERE I.ITEM_ID IN (".$in.")");
																
    oci_execute($query);
	  while($row = oci_fetch_array($query, OCI_ASSOC+OCI_RETURN_NULLS)) {
	    if($row['SECTION_ID']) $sections[] = $row['SECTION_ID'];
	    elseif($row['PRODUCT_ID']) $products[] = $row['PRODUCT_ID'];
	  }
	  $sections = array_unique($sections);
	  $products = array_unique($products);
		
		foreach($sections as $section_id) {
		  $sNid = _srs_data_sync_fetchNodeBySRSId($section_id, 'section');
		  srs_data_sync_logOrEcho('Updating section: '.$sNid);

		  if($sNid) {
				try {
		      $section_node = node_load($sNid);
		      $audit_success = _srs_data_sync_auditFees($section_node, $conn);
			    if(!$audit_success) {
						$ar = (array)$audrows;
			      return srs_data_sync_rollbackAuditAction($xact, $ar[0], 'SALEABLE_ITEM', 'Fee Audit for snid: '.$sNid.' returned false');
			    }
			    else {
					  node_save($section_node);
					  srs_data_sync_logOrEcho('Saved section '.$sNid);
			    }
				}
				catch (Exception $e) {
					$ar = (array)$audrows;
		      return srs_data_sync_rollbackAuditAction($xact, $ar[0], 'SALEABLE_ITEM', 'Could not process sNid: '.$sNid);
				}
		  }
		}

		foreach($products as $product_id) {
		  $pNid = _srs_data_sync_fetchNodeBySRSId($product_id, 'product');
		  srs_data_sync_logOrEcho('Updating product: '.$pNid);
		  		  
		  if($pNid) {
				try {
			  	$product_node = node_load($pNid);
			    $audit_success = _srs_data_sync_auditProdFees($product_node, $conn);
			    if(!$audit_success) {
						$ar = (array)$audrows;
			      return srs_data_sync_rollbackAuditAction($xact, $ar[0], 'SALEABLE_ITEM', 'Fee Audit for sPnid: '.$pNid.' returned false');
			    }
			    else {
					  node_save($product_node);
					  srs_data_sync_logOrEcho('Saved product :'.$pNid);
			    }
				}
				catch (Exception $e) {
					$ar = (array)$audrows;
		      return srs_data_sync_rollbackAuditAction($xact, $ar[0], 'SALEABLE_ITEM', 'Could not process pNid: '.$pNid);
				}
		  }
		}

		//mark success
		foreach($cmp as $aud_id) {
			$cRO = new stdClass();
			$cRO->AUD_ID = $aud_id;
		  srs_data_sync_timestampAuditRow($cRO, 'AUD_SALEABLE_ITEM');
		}

	  //srs_data_sync_logOrEcho('RUNNING FEES COMPLETE');
		return true;
	}
}



function _srs_data_sync_auditFees(&$section_node, &$conn=false) {

	if(!$conn) {
    $conn = srs_data_sync_getSRSConnector();
	  if(!$conn) {
		  srs_data_sync_logOrEcho('Error acquiring SRS Connector');
		  return false;
		}  
  }
  
  $qry = "SELECT * FROM SALEABLE_ITEM SI INNER JOIN ITEM I ON SI.ITEM_ID = I.ITEM_ID INNER JOIN SECTION_COURSE SC ON I.SECTION_COURSE_ID = SC.SECTION_COURSE_ID INNER JOIN SECTION S ON SC.SECTION_ID = S.SECTION_ID INNER JOIN COURSE C ON SC.COURSE_ID = C.COURSE_ID WHERE S.SECTION_ID = ".$section_node->field_srs_id['und'][0]['value']." ORDER BY SI.DEFAULT_FLAG DESC, SI.WEB_ENABLED DESC";
  $query = oci_parse($conn, $qry);
  
  $first_itr = true;
  $child_ids = array();
  $all_child_ids = array();
  $sort_i = 11;
  $attach_sort_i = 20;
  
  oci_execute($query);
  while($row = oci_fetch_array($query, OCI_ASSOC+OCI_RETURN_NULLS)) {

    if($first_itr && (!empty($section_node->field_srs_fee[LANGUAGE_NONE]))) {
      //Delete Existing
      for($i=0; $i<count($section_node->field_srs_fee[LANGUAGE_NONE]); $i++) {
        $fcItem = $section_node->field_srs_fee[LANGUAGE_NONE][$i]['value']; 
        entity_delete_multiple('field_collection_item', array($fcItem));
      }
      unset($section_node->field_srs_fee[LANGUAGE_NONE]);
      $first_itr = false;
    }

    //Fetch New
    if(isset($row['PRICE']) && $row['SALEABLE_ITEM_PARENT_ID'] == NULL) {
		  $fc_item = entity_create('field_collection_item', array('field_name' => 'field_srs_fee'));
		  $fc_item->setHostEntity('node', $section_node);
		  $fc_wrapper = entity_metadata_wrapper('field_collection_item', $fc_item);
		  $fc_wrapper->field_fee_is_attached->set(0);
		  $fc_wrapper->field_fee_is_attached_mandatory->set(0);
		  $fc_wrapper->field_fee_srs_id->set($row['SALEABLE_ITEM_ID']);
		  $fc_wrapper->field_fee_price->set($row['PRICE']);
		  $fc_wrapper->field_fee_label->set($row['SALEABLE_ITEM_DESCRIPTION']);
      $fc_wrapper->field_fee_item_id->set($row['ITEM_ID']);
      $fc_wrapper->field_fee_course_code->set($row['COURSE_CODE']);
      $fc_wrapper->field_fee_web_enabled->set($row['WEB_ENABLED']);
      $fc_wrapper->field_fee_is_default->set($row['DEFAULT_FLAG']);
      $fc_wrapper->field_fee_coupon_enabled->set('N');

		  if($row['TAXES_INCLUDED'] == 'Y') {
		    $fc_wrapper->field_fee_taxes_included->set('Y');
		  }
		  else {
		    $fc_wrapper->field_fee_taxes_included->set('N');
		  }
	
			$taxqry = "SELECT TA.TAX_ID FROM SALEABLE_ITEM SI INNER JOIN TAX_ASN TA ON SI.ITEM_ID = TA.ITEM_ID WHERE SI.SALEABLE_ITEM_ID = ".$row['SALEABLE_ITEM_ID'];
			$taxquery = oci_parse($conn, $taxqry);
	  	oci_execute($taxquery);
	  	while($taxrow = oci_fetch_array($taxquery, OCI_ASSOC+OCI_RETURN_NULLS)) {
        if($taxrow['TAX_ID'] == '1') {
          $fc_wrapper->field_fee_tax_gst->set(1);
        }
        elseif($taxrow['TAX_ID'] == '2') {
          $fc_wrapper->field_fee_tax_pst->set(1);
        }
	  	}
			  
		  if($row['EFFECTIVE_START_DATE']) {
		    $fc_start = intval(strtotime($row['EFFECTIVE_START_DATE']));
		    $fc_wrapper->field_fee_start_date->set($fc_start);
		  }
		  if($row['EFFECTIVE_END_DATE']) {
		    $fc_start = intval(strtotime($row['EFFECTIVE_END_DATE']));
		    $fc_wrapper->field_fee_end_date->set($fc_start);
		  }
		
		  if($row['SORT_ORDER'] != null) {
		    $fc_wrapper->field_fee_sort_order->set($row['SORT_ORDER']);
		  }
		  else {
		    if($row['DEFAULT_FLAG'] == 'Y') {
		      $fc_wrapper->field_fee_sort_order->set(10);
		    }
		    else {
		      $fc_wrapper->field_fee_sort_order->set($sort_i);
		      $sort_i++;
		    }  
		  }
			
		  //check for coupon code	
		  $cpnqry = "SELECT * FROM SPONSOR_TYPE_ASN WHERE SALEABLE_ITEM_ID = ".$row['SALEABLE_ITEM_ID'];
		  $cpnquery = oci_parse($conn, $cpnqry);
	  	  oci_execute($cpnquery);
	  	  if($cpnrow = oci_fetch_array($cpnquery, OCI_ASSOC+OCI_RETURN_NULLS)) {
	        $fc_wrapper->field_fee_coupon_enabled->set('Y');
		  }	
	  
		  //check for attached fees
		  try{
	    	$child_ids = srs_data_sync_fetchAttachedFeeIds($row['SALEABLE_ITEM_ID'], $conn);
		  }
			catch(Exception $e) {
			  srs_data_sync_logOrEcho('Error running fetchAttachedFeeIds() '.$section_node->nid);
				throw $e;
				return false;
			}

	    $all_child_ids = array_merge($all_child_ids, $child_ids);
		  foreach($child_ids as $i => $childarray) {
	    	$fc_wrapper->field_fee_attached_fee_item[$i] = $childarray['id'];
		  }
	
	  	$fc_wrapper->save(true);
	 	}
  } //end while

  try{
  	srs_data_sync_fetchAttachedFees($all_child_ids, $section_node, $attach_sort_i, $conn);
  }
	catch(Exception $e) {
	  srs_data_sync_logOrEcho('Error running fetchAttachedFees() '.$section_node->nid);
		throw $e;
		return false;
	}

  return true;
}


function srs_data_sync_fetchAttachedFeeIds($parent_id, &$conn) {

  $qry = "SELECT * FROM SALEABLE_ITEM WHERE SALEABLE_ITEM_PARENT_ID = ".$parent_id." ORDER BY SALEABLE_ITEM_ID ASC";

  $child_ids = array();

  $query = oci_parse($conn, $qry);
  oci_execute($query);
  
  while($row = oci_fetch_array($query, OCI_ASSOC+OCI_RETURN_NULLS)) {
    $mandatory = ($row['MANDATORY_FLAG'] == "Y")? 1:0;
 		$child = $row['SALEABLE_ITEM_ID'];
    $child_ids[] = array('id' => $child, 'mandatory' => $mandatory);
  }

  return $child_ids;  
}

/*
 * Attach the child saleable item
 */
function srs_data_sync_fetchAttachedFees($child_ids, &$host_node, &$attach_sort_i, &$conn) {
  
  foreach($child_ids as $i => $childarray) {

	  $qry = "SELECT * FROM SALEABLE_ITEM WHERE SALEABLE_ITEM_ID = ".$childarray['id'];
	
	  $query = oci_parse($conn, $qry);
	  oci_execute($query);
	  while($row = oci_fetch_array($query, OCI_ASSOC+OCI_RETURN_NULLS)) {
	
	    if(isset($row['PRICE'])) {
	
			  $fc_item = entity_create('field_collection_item', array('field_name' => 'field_srs_fee'));
			  $fc_item->setHostEntity('node', $host_node);
			  $fc_wrapper = entity_metadata_wrapper('field_collection_item', $fc_item);
			  $fc_wrapper->field_fee_is_attached->set(1);
			  $fc_wrapper->field_fee_is_attached_mandatory->set($childarray['mandatory']);
			  $fc_wrapper->field_fee_srs_id->set($row['SALEABLE_ITEM_ID']);
			  $fc_wrapper->field_fee_price->set($row['PRICE']);
			  $fc_wrapper->field_fee_label->set($row['SALEABLE_ITEM_DESCRIPTION']);
	      $fc_wrapper->field_fee_item_id->set($row['ITEM_ID']);
	      $fc_wrapper->field_fee_web_enabled->set($row['WEB_ENABLED']);
	      $fc_wrapper->field_fee_is_default->set($row['DEFAULT_FLAG']);
	      $fc_wrapper->field_fee_coupon_enabled->set('N');
	
		  $fc_wrapper->field_fee_sort_order->set($attach_sort_i);
		  $attach_sort_i++;

		  //try to override the label if: Default, Standard Fee, Program Fee
		  if(($row['SALEABLE_ITEM_DESCRIPTION'] == 'Default') || ($row['SALEABLE_ITEM_DESCRIPTION'] == 'Standard Fee') || ((strpos($row['SALEABLE_ITEM_DESCRIPTION'], 'Program Fee') !== FALSE))) {
			$attqry = "SELECT * FROM SALEABLE_ITEM SI INNER JOIN ITEM I ON SI.ITEM_ID = I.ITEM_ID INNER JOIN SECTION_COURSE SC ON I.SECTION_COURSE_ID = SC.SECTION_COURSE_ID INNER JOIN COURSE C ON C.COURSE_ID = SC.COURSE_ID WHERE SI.SALEABLE_ITEM_ID = ".$row['SALEABLE_ITEM_ID'];

	  		$attquery = oci_parse($conn, $attqry);
	  		oci_execute($attquery);
		    while($attrow = oci_fetch_array($attquery, OCI_ASSOC+OCI_RETURN_NULLS)) {
		      if($attrow['COURSE_TITLE']) {
		   		$fc_wrapper->field_fee_label->set($attrow['COURSE_TITLE']);
		      }
		    }
		  }
	
		  if($row['TAXES_INCLUDED'] == 'Y') {
		    $fc_wrapper->field_fee_taxes_included->set('Y');
		  }
		  else {
		    $fc_wrapper->field_fee_taxes_included->set('N');
		  }
	
			$taxqry = "SELECT TA.TAX_ID FROM SALEABLE_ITEM SI INNER JOIN TAX_ASN TA ON SI.ITEM_ID = TA.ITEM_ID WHERE SI.SALEABLE_ITEM_ID = ".$row['SALEABLE_ITEM_ID'];
			$taxquery = oci_parse($conn, $taxqry);
	  	oci_execute($taxquery);
	  	while($taxrow = oci_fetch_array($taxquery, OCI_ASSOC+OCI_RETURN_NULLS)) {
        if($taxrow['TAX_ID'] == '1') {
          $fc_wrapper->field_fee_tax_gst->set(1);
        }
        elseif($taxrow['TAX_ID'] == '2') {
          $fc_wrapper->field_fee_tax_pst->set(1);
        }
	  	}
	  				  
		  if($row['EFFECTIVE_START_DATE']) {
		    $fc_start = intval(strtotime($row['EFFECTIVE_START_DATE']));
		    $fc_wrapper->field_fee_start_date->set($fc_start);
		  }
		  if($row['EFFECTIVE_END_DATE']) {
		    $fc_start = intval(strtotime($row['EFFECTIVE_END_DATE']));
		    $fc_wrapper->field_fee_end_date->set($fc_start);
		  }
		  
	 	  $fc_wrapper->save(true);
	    }
	
	  }
  }
  return true;
}

function _srs_data_sync_auditProdFees(&$product_node, &$conn=false) {
 
	if(!$conn) {
    $conn = srs_data_sync_getSRSConnector();
	  if(!$conn) {
		  srs_data_sync_logOrEcho('Error acquiring SRS Connector');
		  return false;
		}  
  }
	  
  $qry = "SELECT * FROM SALEABLE_ITEM SI INNER JOIN ITEM I ON SI.ITEM_ID = I.ITEM_ID INNER JOIN PRODUCT P ON I.PRODUCT_ID = P.PRODUCT_ID WHERE P.PRODUCT_ID = ".$product_node->field_srs_id['und'][0]['value']." ORDER BY SI.DEFAULT_FLAG DESC, SI.WEB_ENABLED DESC";  
  $query = oci_parse($conn, $qry);
  
  $first_itr = true;
  $child_ids = array();
  $sort_i = 11;
  $attach_sort_i = 20;
  
  oci_execute($query);
  while($row = oci_fetch_array($query, OCI_ASSOC+OCI_RETURN_NULLS)) {

    if($first_itr && (!empty($product_node->field_srs_fee[LANGUAGE_NONE]))) {
      //Delete Existing
      for($i=0; $i<count($product_node->field_srs_fee[LANGUAGE_NONE]); $i++) {
        $fcItem = $product_node->field_srs_fee[LANGUAGE_NONE][$i]['value']; 
        entity_delete_multiple('field_collection_item', array($fcItem));
      }
      unset($product_node->field_srs_fee[LANGUAGE_NONE]);
      $first_itr = false;
    }

    //Fetch New
    if(isset($row['PRICE']) && $row['SALEABLE_ITEM_PARENT_ID'] == NULL) {
		  $fc_item = entity_create('field_collection_item', array('field_name' => 'field_srs_fee'));
		  $fc_item->setHostEntity('node', $product_node);
		  $fc_wrapper = entity_metadata_wrapper('field_collection_item', $fc_item);
	 	  $fc_wrapper->field_fee_is_attached->set(0);
	 	  $fc_wrapper->field_fee_is_attached_mandatory->set(0);
		  $fc_wrapper->field_fee_srs_id->set($row['SALEABLE_ITEM_ID']);
		  $fc_wrapper->field_fee_price->set($row['PRICE']);
		  $fc_wrapper->field_fee_label->set($row['SALEABLE_ITEM_DESCRIPTION']);
      $fc_wrapper->field_fee_item_id->set($row['ITEM_ID']);
      $fc_wrapper->field_fee_product_code->set($row['PRODUCT_CODE']);
      $fc_wrapper->field_fee_web_enabled->set($row['WEB_ENABLED']);
      $fc_wrapper->field_fee_is_default->set($row['DEFAULT_FLAG']);
      $fc_wrapper->field_fee_coupon_enabled->set('N');

		  if($row['TAXES_INCLUDED'] == 'Y') {
		    $fc_wrapper->field_fee_taxes_included->set('Y');
		  }
		  else {
		    $fc_wrapper->field_fee_taxes_included->set('N');
		  }
	
			$taxqry = "SELECT TA.TAX_ID FROM SALEABLE_ITEM SI INNER JOIN TAX_ASN TA ON SI.ITEM_ID = TA.ITEM_ID WHERE SI.SALEABLE_ITEM_ID = ".$row['SALEABLE_ITEM_ID'];
			$taxquery = oci_parse($conn, $taxqry);
	  	oci_execute($taxquery);
	  	while($taxrow = oci_fetch_array($taxquery, OCI_ASSOC+OCI_RETURN_NULLS)) {
        if($taxrow['TAX_ID'] == '1') {
          $fc_wrapper->field_fee_tax_gst->set(1);
        }
        elseif($taxrow['TAX_ID'] == '2') {
          $fc_wrapper->field_fee_tax_pst->set(1);
        }
	  	}
	  				  
		  if($row['EFFECTIVE_START_DATE']) {
		    $fc_start = intval(strtotime($row['EFFECTIVE_START_DATE']));
		    $fc_wrapper->field_fee_start_date->set($fc_start);
		  }
		  if($row['EFFECTIVE_END_DATE']) {
		    $fc_start = intval(strtotime($row['EFFECTIVE_END_DATE']));
		    $fc_wrapper->field_fee_end_date->set($fc_start);
		  }
	
	 	  if($row['SORT_ORDER'] != null) {
		    $fc_wrapper->field_fee_sort_order->set($row['SORT_ORDER']);
		  }
		  else {
		    if($row['DEFAULT_FLAG'] == 'Y') {
	  	      $fc_wrapper->field_fee_sort_order->set(10);
		    }
		    else {
	  	      $fc_wrapper->field_fee_sort_order->set($sort_i);
		      $sort_i++;
		    }
		  }
		  		
		  //check for coupon code	
		  $cpnqry = "SELECT * FROM SPONSOR_TYPE_ASN WHERE SALEABLE_ITEM_ID = ".$row['SALEABLE_ITEM_ID'];
		  $cpnquery = oci_parse($conn, $cpnqry);
	  	  oci_execute($cpnquery);
	  	  if($cpnrow = oci_fetch_array($cpnquery, OCI_ASSOC+OCI_RETURN_NULLS)) {
	        $fc_wrapper->field_fee_coupon_enabled->set('Y');
		  }	
	
  	  //check for attached fees
		  try{
	      $child_ids = srs_data_sync_fetchAttachedFeeIds($row['SALEABLE_ITEM_ID'], $conn);
		  }
			catch(Exception $e) {
			  srs_data_sync_logOrEcho('Error running fetchAttachedFeeIds() '.$product_node->nid);
				throw $e;
				return false;
			}

  	  foreach($child_ids as $i => $childarray) {
	    	$fc_wrapper->field_fee_attached_fee_item[$i] = $childarray['id'];
  	  }

		  $fc_wrapper->save(true);
	 	}
  } //end while


  try{
	  srs_data_sync_fetchAttachedFees($child_ids, $product_node, $attach_sort_i, $conn);
  }
	catch(Exception $e) {
	  srs_data_sync_logOrEcho('Error running fetchAttachedFees() '.$product_node->nid);
		throw $e;
		return false;
	}


  return true;
}






////////////////////////////////////////////////////////////////


function srs_data_sync_importRelationships($xact) {
  srs_data_sync_logOrEcho('RUNNING RELATIONSHIPS');

  $rels = srs_data_sync_getSynchRelationships();
  
  foreach($rels as $i => $rel_array) {
    $audit_table_name = 'AUD_'.$rel_array['name'];

    if($audrows = db_query(sprintf("SELECT * FROM %s WHERE drupal_import_time IS NULL".LIMIT_RESULTS." FOR UPDATE;", $audit_table_name))) {

      foreach($audrows as $audrow) {
			  srs_data_sync_logOrEcho('ACTION: '.$audrow->AUD_ACTION." :: TYPE: ".$audit_table_name);
      	
      	//get target and ref nodes
			  $refId = strtoupper($rel_array['ref']).'_ID';
  			$tarId = strtoupper($rel_array['target']).'_ID';  
  	
				//load target node
				$efQuery = new EntityFieldQuery;
				$entity = $efQuery
		  	  ->entityCondition('entity_type', 'node')
		  	  ->entityCondition('bundle', $rel_array['target'])
		  	  ->fieldCondition('field_srs_id', 'value', $audrow->$tarId, '=')
		  	  ->execute();

				if(is_null(key($entity['node']))) {
				  if($audrow->AUD_ACTION == 'D') {
					  srs_data_sync_logOrEcho('target node was deleted prior to running the delete on the node reference');
					  srs_data_sync_timestampAuditRow($audrow, $audit_table_name);
				    continue;
				  } 
				  else {
					  srs_data_sync_logOrEcho('key for target entity node is empty');
					  return srs_data_sync_rollbackAuditAction($xact, $audrow, $audit_table_name);
				  }
				} 
				$targetNode = node_load(key($entity['node']));
				if($targetNode == FALSE) {
				  srs_data_sync_logOrEcho('Could not fetch target node: '.key($entity['node']));
				  return srs_data_sync_rollbackAuditAction($xact, $audrow, $audit_table_name);
				} 
	
				//load ref node
				$efQuery = new EntityFieldQuery;
				$entity = $efQuery
		  	  ->entityCondition('entity_type', 'node')
		  	  ->entityCondition('bundle', $rel_array['ref'])
		  	  ->fieldCondition('field_srs_id', 'value', $audrow->$refId, '=')
		  	  ->execute();
			
				if(is_null(key($entity['node']))) {
				  if($audrow->AUD_ACTION == 'D') {
					  srs_data_sync_logOrEcho('ref node was deleted prior to running the delete on the node reference');
					  srs_data_sync_timestampAuditRow($audrow, $audit_table_name);
				    continue;
				  } 
				  else {
					  srs_data_sync_logOrEcho('key for ref entity node is empty');
						return srs_data_sync_rollbackAuditAction($xact, $audrow, $audit_table_name);
				  }
				} 

				$refNode = node_load(key($entity['node']));
				if($refNode == FALSE) {
				  srs_data_sync_logOrEcho('Could not fetch ref node: '.key($entity['node']));
					return srs_data_sync_rollbackAuditAction($xact, $audrow, $audit_table_name);
				} 

				//process audit row      	
      	if(srs_data_sync_processRelAuditAction($audrow->AUD_ACTION, $audrow, $rel_array, $targetNode, $refNode)) {
					srs_data_sync_timestampAuditRow($audrow, $audit_table_name);
        }
        else {
          return srs_data_sync_rollbackAuditAction($xact, $audrow, $audit_table_name);
        }
      	
			}
		}
  }
  
  return true;  
}

function srs_data_sync_processRelAuditAction($action, $audrow, $rel_array, $targetNode, $refNode) {

	if($action == 'U') return srs_data_sync_relUpdateOrInsert($audrow, $rel_array, $targetNode, $refNode);
	elseif($action == 'I') return srs_data_sync_relUpdateOrInsert($audrow, $rel_array, $targetNode, $refNode);
	elseif($action == 'D') return srs_data_sync_relDelete($rel_array, $targetNode, $refNode);
}

function srs_data_sync_maintainInstructorSectionRoles($rel_array, $targetNode, $refNode, $audrow) {
//echo 'MAINTAIN'."\n";
  $relField = $rel_array['field'];
  $_temp = &$targetNode->$relField;

  $capField = strtoupper($rel_array['rel_cap']);
  $targetCapField = $rel_array['source_cap'];
  $_tempCap = &$targetNode->$targetCapField;

  //maintain existing roles
  $rolemap = array();
  foreach($_temp['und'] as $index => $role) {
    if(isset($_temp['und'][$index]['nid'])) {
      $rolemap[$_temp['und'][$index]['nid']] = $_tempCap['und'][$index]['value'];
    }
  }
  
  //override with new role
  $rolemap[$refNode->nid] = $audrow->$capField;
  
//print_r($rolemap);
  return $rolemap;
  
}

function srs_data_sync_setInstructorSectionRoles($rel_array, $targetNode, $rolemap) {

  $relField = $rel_array['field'];
  $_temp = &$targetNode->$relField;

  $capField = strtoupper($rel_array['rel_cap']);
  $targetCapField = $rel_array['source_cap'];
  $_tempCap = &$targetNode->$targetCapField;
  unset($_tempCap['und']);
  
  foreach($_temp['und'] as $index => $nidvalArray) {
//echo "Nidval: ".$nidvalArray['nid']."\n"; 
    if(array_key_exists($nidvalArray['nid'], $rolemap)) {
//echo 'Rolemap val: '.$rolemap[$nidvalArray['nid']]."\n";
      $_tempCap['und'][$index]['value'] = $rolemap[$nidvalArray['nid']];
    }
  }
//echo 'RESTORED ROLEMAP'."\n";
}

function srs_data_sync_relUpdateOrInsert($audrow, $rel_array, $targetNode, $refNode) {

  $relField = $rel_array['field'];
  $_temp = &$targetNode->$relField;
	$refNid = $refNode->nid;

	$capture = ($rel_array['source_cap'] && $rel_array['rel_cap']);

  /* HANDLE EXCEPTIONS */
  
  if($rel_array['name'] == 'INSTRUCTOR_SECTION') {
    $rolemap = srs_data_sync_maintainInstructorSectionRoles($rel_array, $targetNode, $refNode, $audrow);
  }

  //Solves the case where in SRS, deleting a lone instructor creates an UPDATE action where it replaces
  //the old instructor with the TBA instructor
  if(($audrow->AUD_ACTION == 'U') && ($rel_array['name'] == 'INSTRUCTOR_SECTION') && ($audrow->INSTRUCTOR_ID == INSTRUCTOR_SECTION_TBA_ID)) {
    srs_data_sync_runFunkySRSUpdateAction($rel_array, $targetNode);
  }
  //Solves the case where in SRS, changing the location for a section creates an UPDATE action, where it replaces 
  //the old location and puts in the new one
  if(($audrow->AUD_ACTION == 'U') && ($rel_array['name'] == 'LOCATION_SECTION')) {
    srs_data_sync_runFunkySRSUpdateAction($rel_array, $targetNode);
  }

  /* /HANDLE EXCEPTIONS */

  $nid_exists = false;
  $nid_index = false;

  //test to see if the nid exists already
  for($i=0; $i<count($_temp['und']); $i++) {
    if($_temp['und'][$i]['nid'] == $refNid) {
      $nid_index = $i;
      $nid_exists = true;
    } 
  }

	if(!$nid_exists) $_temp['und'][]['nid'] = $refNid;

  //update target node
  if(($capture) && ($rel_array['name'] != 'INSTRUCTOR_SECTION')) {
    $capField = strtoupper($rel_array['rel_cap']);
    $targetCapField = $rel_array['source_cap'];
    $_tempCap = &$targetNode->$targetCapField;
      
    //edit the same index as the existing nid
    if($nid_index) {
      $_tempCap['und'][$nid_index]['value'] = $audrow->$capField;
    }
    //this is new data
    else {
      //$_temp['und'][]['nid'] = $refNid;
      $_tempCap['und'][0]['value'] = $audrow->$capField;
    }
  }

  if($rel_array['name'] == 'INSTRUCTOR_SECTION') {
    srs_data_sync_setInstructorSectionRoles($rel_array, $targetNode, $rolemap);
  }
	try{
		node_save($targetNode);
	}
	catch (Exception $e) {
	  return false;
	}
  return true;
}

function srs_data_sync_runFunkySRSUpdateAction($rel_array, $targetNode) {

  $relField = $rel_array['field'];
  $_temp = &$targetNode->$relField;

  $index = 0;
  do {
  	unset($_temp['und'][$index]);
    $index++;
  } while(is_array($_temp['und'][$index]));

}

function srs_data_sync_relDelete($rel_array, $targetNode, $refNode) {

  if($rel_array['name'] == 'INSTRUCTOR_SECTION') {
    $rolemap = srs_data_sync_maintainInstructorSectionRoles($rel_array, $targetNode, $refNode, $audrow);
  }

  $relField = $rel_array['field'];
  $_temp = &$targetNode->$relField;
	$refNid = $refNode->nid;

  $index = 0;
  do {
  	if($_temp['und'][$index]['nid'] == $refNid) unset($_temp['und'][$index]);
    $index++;
  } while(is_array($_temp['und'][$index]));

  if($rel_array['name'] == 'INSTRUCTOR_SECTION') {
    srs_data_sync_setInstructorSectionRoles($rel_array, $targetNode, $rolemap);
  }

	try{
		node_save($targetNode);
	}
	catch (Exception $e) {
	  return false;
	}

  return true;
}










/*
 * Import bundles
 * return false and rollback transaction || return true
 */
function srs_data_sync_importBundles($xact) {
	srs_data_sync_logOrEcho('RUNNING BUNDLES');

  $bundles = srs_data_sync_getSynchBundles();
  $fields = srs_data_sync_getSynchFields();


  foreach($bundles as $ora_name => $dr_name) {
    $audit_table_name = 'AUD_'.$ora_name;
    if($audrows = db_query(sprintf("SELECT * FROM %s WHERE drupal_import_time IS NULL".LIMIT_RESULTS." FOR UPDATE;", $audit_table_name))) {

      foreach($audrows as $audrow) {
				srs_data_sync_logOrEcho('ACTION: '.$audrow->AUD_ACTION." :: TYPE: ".$bundles[$ora_name]);
        
        //process updates
        if(srs_data_sync_processBundleAuditAction($audrow->AUD_ACTION, $audrow, $bundles[$ora_name], $fields[$dr_name])) {
						srs_data_sync_timestampAuditRow($audrow, $audit_table_name);
        }
      	else {
          return srs_data_sync_rollbackAuditAction($xact, $audrow, $audit_table_name);
      	}

      }
    }
  }

  return true;
}

function srs_data_sync_processBundleAuditAction($action, $audrow, $ora_name, $fields) {

	if($action == 'U') return srs_data_sync_nodeUpdate($audrow, $ora_name, $fields);
	elseif($action == 'I') return srs_data_sync_nodeInsert($audrow, $ora_name, $fields);
	elseif($action == 'D') return srs_data_sync_nodeDelete($audrow, $ora_name);
}

/*
 * Run a 'U' AUD_ACTION on a base table
 */
function srs_data_sync_nodeUpdate($row, $type, $fields) {

  $srs_id_col_name = strtoupper($type).'_ID';
	$drupal_nid = _srs_data_sync_fetchNodeBySRSId($row->$srs_id_col_name, $type);
	srs_data_sync_logOrEcho('Drupal NID: '.$drupal_nid);

	if($drupal_nid) {

  	$node = node_load($drupal_nid);
		if($node !== FALSE) {
	
			try {
			  srs_data_sync_hlpUpdateSetFieldsByObject($fields, $row, $node);
			  node_save($node);
			  
			  if($type == 'section') {
			  	$audit_success = _srs_data_sync_auditFees($node);
			    if(!$audit_success) return false;
			  }
			  elseif($type == 'product') {
			  	$audit_success = _srs_data_sync_auditProdFees($node);
			    if(!$audit_success) return false;
			  }
			  			  
			}
			catch (Exception $e) {
			  return false;
			}
		  return true;
		}
	}
	else {
		srs_data_sync_logOrEcho('Tried to update a node that does not exist, attempting to insert');
	  return srs_data_sync_nodeInsert($row, $type, $fields);
	}

	return false;
}

/*
 * Run a 'I' AUD_ACTION on a base table
 */
function srs_data_sync_nodeInsert($row, $type, $fields) {

  $srs_id_col_name = strtoupper($type).'_ID';
	srs_data_sync_logOrEcho('FETCH BY SRS ID: '.$row->$srs_id_col_name);
    
	$drupal_nid = _srs_data_sync_fetchNodeBySRSId($row->$srs_id_col_name, $type);

  if(empty($drupal_nid)) {
		try{
      $nid = srs_data_sync_createNodeFromSRSObject($fields, $row, $type);
		}
		catch (Exception $e) {
		  return false;
		}
		srs_data_sync_logOrEcho('NEW DRUPAL NID: '.$nid);
    return true;
  }
  
	srs_data_sync_logOrEcho('->NID EXIST ON INSERT: '.$drupal_nid);
  return false;
}

/*
 * Run a 'D' AUD_ACTION on a base table
 */
function srs_data_sync_nodeDelete($row, $type) {

  $srs_id_col_name = strtoupper($type).'_ID';
	$drupal_nid = _srs_data_sync_fetchNodeBySRSId($row->$srs_id_col_name, $type);

  if($drupal_nid) {
		try{
	    node_delete($drupal_nid);
		}
		catch (Exception $e) {
		  return false;
		}
  }
		
	return true;
}



function srs_data_sync_importRequisiteRelationships($xact) {
	srs_data_sync_logOrEcho('RUNNING REQUISITES');

  $audrows = db_query(sprintf("SELECT * FROM %s WHERE drupal_import_time IS NULL".LIMIT_RESULTS." FOR UPDATE;", 'AUD_REQUISITE'));
  
  $courseList = array();
  $update = false;

  foreach($audrows as $audrow) {

		if(!$conn) {
	  	$conn = srs_data_sync_getSRSConnector();
	  	if(!$conn) {
			  $xact->rollback();
			  srs_data_sync_alertError('connector', 'fees');
				
				srs_data_sync_logOrEcho('Error acquiring SRS Connector');
				srs_data_sync_logOrEcho('Rolled back');
			  return false;
	  	}
	  }

		if(!in_array($audrow->PARENT_SEQUENCE_ID, $courseList)) {
			try {
	    	$req = srs_data_sync_getCourseRequisites($audrow->PARENT_SEQUENCE_ID, $conn);
				if(!$req) {
				  $xact->rollback();
				  srs_data_sync_alertError('AUD_REQ', 'requisite');
					srs_data_sync_logOrEcho('Could not process requisite');
					srs_data_sync_logOrEcho('Rolled back');
				  return false;
				}
	    	
	    	srs_data_sync_timestampAuditRow($audrow, 'AUD_REQUISITE');
	    	$update = true;
	    	$courseList[] = $audrow->PARENT_SEQUENCE_ID;
			}
			catch (Exception $e) {
			  return false;
			}
		}
		else { //already ran this course
	    srs_data_sync_timestampAuditRow($audrow, 'AUD_REQUISITE');
		}

  }

	if($update) {
		cache_clear_all('*', 'cache_views', TRUE);
		cache_clear_all('*', 'cache_views_data', TRUE);
		srs_data_sync_logOrEcho('Cleared Cache Views');
	}

  return true;
}

function srs_data_sync_getCourseRequisites($parent_id, &$conn) {
  
  $course_nid = _srs_data_sync_fetchNodeBySRSId($parent_id, 'course', 2);
  if($course_nid == false) {
		srs_data_sync_logOrEcho('Could not load requisite parent');
    return false;
  } 

	$node = node_load($course_nid);
  if($node !== FALSE) {
	  $course_srsId = $node->field_srs_id['und'][0]['value'];
	  $first_itr = true;
	  $first_grp_itr = true;
	  $empty_result_set = true;  
	  
	  //we need to know all of the data for this course, so fetch from SRS
	  $qry = "SELECT * FROM REQUISITE WHERE PARENT_OBJECT_TYPE_ID = 3 AND PARENT_SEQUENCE_ID = ".$course_srsId;
	  $query = oci_parse($conn, $qry);
	  oci_execute($query);
	  while($row = oci_fetch_array($query, OCI_ASSOC+OCI_RETURN_NULLS)) {
	    $empty_result_set = false;
	    
	    //requisite course has srsid = $row['CHILD_SEQUENCE_ID']
	    if($row['CHILD_OBJECT_TYPE_ID'] == 3) {
	      $req_nid = _srs_data_sync_fetchNodeBySRSId($row['CHILD_SEQUENCE_ID'], 'course', 2);
	      if($req_nid == false) {
					srs_data_sync_logOrEcho('CHILD_OBJECT_TYPE_ID:3 no req nid');
		      return false;
	      } 
	
		 	  if($first_itr) {
		    	unset($node->field_srs_requisite_course['und']);
		      unset($node->field_srs_requisite_product['und']);
		      $first_itr = false;
		    }
		 	  if($first_grp_itr) {
		      unset($node->field_srs_requisite_group_label['und']);
		      unset($node->field_srs_requisite_group_num['und']);
		      unset($node->field_srs_requisite_group_items['und']);
		    }
		
			  $node->field_srs_requisite_course['und'][]['nid'] = $req_nid;
	    }
	    //requisite product has srsid = $row['CHILD_SEQUENCE_ID']
	    elseif($row['CHILD_OBJECT_TYPE_ID'] == 5) {
	      $req_nid = _srs_data_sync_fetchNodeBySRSId($row['CHILD_SEQUENCE_ID'], 'product', 2);
	      if($req_nid == false) {
					srs_data_sync_logOrEcho('CHILD_OBJECT_TYPE_ID:5 no req nid');
		      return false;
	      } 
	
		 	  if($first_itr) {
		  	  srs_data_sync_logTransaction('RESET COURSE REQS', '<SRS SPYDER>', 2);
		      unset($node->field_srs_requisite_course['und']);
		      unset($node->field_srs_requisite_product['und']);
		      $first_itr = false;
		    }
		 	  if($first_grp_itr) {
		      unset($node->field_srs_requisite_group_label['und']);
		      unset($node->field_srs_requisite_group_num['und']);
		      unset($node->field_srs_requisite_group_items['und']);
		    }
			  
			  $node->field_srs_requisite_product['und'][]['nid'] = $req_nid;
	    }
	    //requisite group has srsid = $row['CHILD_SEQUENCE_ID']
	    elseif($row['CHILD_OBJECT_TYPE_ID'] == 10) {
	      $group_id = $row['CHILD_SEQUENCE_ID'];
	  	  $group_qry = "SELECT * FROM REQ_GROUP_ITEM RGI INNER JOIN REQ_GROUP_TYPE RGT ON RGI.REQ_GROUP_TYPE_ID = RGT.REQ_GROUP_TYPE_ID WHERE RGI.REQ_GROUP_TYPE_ID = ".$group_id;
	  	  
	  	  $group_query = oci_parse($conn, $group_qry);
	  	  oci_execute($group_query);
	  	  //requisite course has srsid = $grouprow['SEQUENCE_ID']
	  	  while($grouprow = oci_fetch_array($group_query, OCI_ASSOC+OCI_RETURN_NULLS)) {
	        $req_nid = _srs_data_sync_fetchNodeBySRSId($grouprow['SEQUENCE_ID'], 'course', 2);
		      if($req_nid == false) {
						srs_data_sync_logOrEcho('CHILD_OBJECT_TYPE_ID:10 no req nid');
			      return false;
		      } 
		
	 	    	if($first_grp_itr) {
	          unset($node->field_srs_requisite_group_items['und']);
			      $node->field_srs_requisite_group_label['und'][]['value'] = $grouprow['REQ_GROUP_TYPE_SHORT_DESC'];
			      $node->field_srs_requisite_group_num['und'][]['value'] = $row['NUM_REQUIRED'];
	          $first_grp_itr = false;
	        }
	 	    	if($first_itr) {
	          unset($node->field_srs_requisite_course['und']);
	          unset($node->field_srs_requisite_product['und']);
	        }
	
		    	$node->field_srs_requisite_group_items['und'][]['nid'] = $req_nid;
	  	  }
	    }
	  }
	
	  if ($empty_result_set) {
	    unset($node->field_srs_requisite_course['und']);
	    unset($node->field_srs_requisite_product['und']);
	    unset($node->field_srs_requisite_group_label['und']);
	    unset($node->field_srs_requisite_group_num['und']);
	    unset($node->field_srs_requisite_group_items['und']);
	  }
	  
	  try {
	  	node_save($node);
	  }
	  catch (Exception $e) {
	    throw $e;
	  }
  
  } // end if($node !== FALSE)
  return true;
}



/*
 * Helper function which sets the sync fields for a Drupal node
 */
function srs_data_sync_hlpUpdateSetFieldsByObject($fields, $row, &$updateNode) {
  //$fields = srs_data_sync_sanitizeFields($fields);

  foreach($fields as $oracol => $drupalfield) {

    $isFiltered = _srs_data_sync_IsFilteredHTML($drupalfield);
    $oracol = trim($oracol, '_');
    $_srsfield = $row->$oracol;
          
    //Drupal title field is set differently than the rest
    if($drupalfield == 'title') {
      $updateNode->title = $_srsfield;
    } 
    else {
	    //Need to set field language value by reference to avoid scalar type offset error
	    $_temp = &$updateNode->$drupalfield;
      $_temp['und'][0]['value'] = $_srsfield;
      if($isFiltered) $_temp['und'][0]['format'] = 'filtered_html';
    }
  }
  return true;
}

/*
 * Creates a single node, based on SRS row object
 */
function srs_data_sync_createNodeFromSRSObject($fields, $srsobj, $type) {

  $node = new stdClass();
  $node->type = $type;
  $node->language = 'en';
  $node->revision = 0;
  $node->uid = 1;
  $node->status = 0;
  	  
  srs_data_sync_hlpUpdateSetFieldsByObject($fields, $srsobj, $node);

  //TODO: Attach Instructor Names (Requires OCI)
  /*if($type == 'instructor') {
    try{
      srs_data_sync_AttachInstructorNameByObject($srsobj->INSTRUCTOR_ID, $node);
    }
		catch (Exception $e) {
	    throw $e;
	    return;
		}
  } */

  try{
    node_save($node);
	}
	catch (Exception $e) {
    throw $e;
	}
  return $node->nid;
}

/*
 * Run one-off function to attach a new Instructor's name to the node
 * Unfortunately this is hard coded, but it's a unique work around case...
 */
function srs_data_sync_AttachInstructorNameByObject($instructor_id, &$updateNode) {
  $conn = srs_data_sync_getSRSConnector();

  $query = oci_parse($conn, "SELECT P.SURNAME, P.FIRST_GIVEN_NAME FROM PERSON P INNER JOIN INSTRUCTOR I ON P.CLIENT_ID = I.CLIENT_ID WHERE I.INSTRUCTOR_ID = ".$instructor_id);

  if(!oci_execute($query)) {
    throw new Exception('Could not execute instructor SRS query');
    return false;
  }

  $row = oci_fetch_array($query, OCI_ASSOC+OCI_RETURN_NULLS);
  if(array_key_exists('SURNAME', $row)) {
    $updateNode->field_instructor_last_name['und'][]['value'] = $row['SURNAME'];
  }
  if(array_key_exists('FIRST_GIVEN_NAME', $row)) {
    $updateNode->field_instructor_first_name['und'][]['value'] = $row['FIRST_GIVEN_NAME'];
  }
  return true;
}


function srs_data_sync_sanitizeFields($fields) {
  unset($fields['EXCLUDE_FIELD']);
  unset($fields['EXCLUDE_FIELD_VALUE']);
  return $fields;
}
