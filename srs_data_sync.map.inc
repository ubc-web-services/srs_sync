<?php

function srs_data_sync_getSynchBundles() {
	$bundles = array();
	$bundles['DOW'] = 'dow';
	$bundles['TERM'] = 'term';
	$bundles['LOCATION'] = 'location';
	$bundles['INSTRUCTOR'] = 'instructor';
	$bundles['COURSE'] = 'course';
	$bundles['PRODUCT'] = 'product';
	$bundles['SECTION'] = 'section';
	return $bundles;
}

function srs_data_sync_getSynchRelationships() {
  $rels = array();
  $rels[0]['name'] = 'SECTION_DOW';
	$rels[0]['target'] = 'section';
	$rels[0]['ref'] = 'dow';
  $rels[0]['field'] = 'field_srs_section_dow';
	$rels[0]['source_cap'] = null;
	$rels[0]['rel_cap'] = null;

  $rels[1]['name'] = 'INSTRUCTOR_SECTION';
	$rels[1]['target'] = 'section';
	$rels[1]['ref'] = 'instructor';
  $rels[1]['field'] = 'field_srs_section_instructor';
	$rels[1]['source_cap'] = 'field_srs_section_instructor_rol'; //many to many
	$rels[1]['rel_cap'] = 'INSTRUCTOR_ROLE_ID';

  $rels[2]['name'] = 'LOCATION_SECTION';
	$rels[2]['target'] = 'section';
	$rels[2]['ref'] = 'location';
  $rels[2]['field'] = 'field_srs_section_location';
	$rels[2]['source_cap'] = 'field_srs_section_room'; //1 to 1
	$rels[2]['rel_cap'] = 'ROOM_DESCRIPTION';

  $rels[3]['name'] = 'SECTION_COURSE';
	$rels[3]['target'] = 'section';
	$rels[3]['ref'] = 'course';
  $rels[3]['field'] = 'field_section_course';
	$rels[3]['source_cap'] = 'field_srs_section_course_id'; //1 to 1
	$rels[3]['rel_cap'] = 'SECTION_COURSE_ID';

	return $rels;
}

function srs_data_sync_getSynchFields() {
	$fields = array();

	//$fields['location']['EXCLUDE_FIELD'] = 'ACTIVE_FLAG';
	//$fields['location']['EXCLUDE_FIELD_VALUE'] = 'Y';
	$fields['location']['LOCATION_ID'] = 'field_srs_id';
	$fields['location']['BUILDING_NAME'] = 'title';
	$fields['location']['BUILDING_ADDRESS'] = 'field_location_building_address';
	$fields['location']['ACTIVE_FLAG'] = 'field_srs_location_active';

	//$fields['course']['EXCLUDE_FIELD'] = 'WEB_FLAG';
	//$fields['course']['EXCLUDE_FIELD_VALUE'] = 'Y';
	$fields['course']['COURSE_ID'] = 'field_srs_id';
	$fields['course']['COURSE_CODE'] = 'field_srs_course_code';
	$fields['course']['WEB_DESCRIPTION'] = 'field_srs_course_summary';
	$fields['course']['CALENDAR_DESCRIPTION'] = 'field_srs_course_cal_descr';
	$fields['course']['FORMAT'] = 'field_srs_course_format';
	$fields['course']['COURSE_TITLE'] = 'title';
	$fields['course']['SEARCH_KEYWORDS'] = 'field_srs_course_search_keywords';
	$fields['course']['WEB_FLAG'] = 'field_srs_course_web_flag';

	//$fields['product']['EXCLUDE_FIELD'] = 'STATUS_CODE';
	//$fields['product']['EXCLUDE_FIELD_VALUE'] = 'O';
	$fields['product']['PRODUCT_ID'] = 'field_srs_id';
	$fields['product']['PRODUCT_TITLE'] = 'title';
	$fields['product']['WEB_DESCRIPTION'] = 'field_srs_product_summary';
	$fields['product']['PRODUCT_DESCRIPTION'] = 'field_srs_product_cal_desc';
	$fields['product']['PRODUCT_CODE'] = 'field_product_code';
	$fields['product']['START_DATE'] = 'field_product_start_date';
	$fields['product']['END_DATE'] = 'field_product_end_date';
	$fields['product']['CURRENT_QTY'] = 'field_product_current_quantity';
	$fields['product']['MAXIMUM_QTY'] = 'field_product_maximum_quantity';
	$fields['product']['STATUS_CODE'] = 'field_srs_product_status_code';
	$fields['product']['WEB_FLAG'] = 'field_srs_product_web_flag';

	$fields['term']['TERM_ID'] = 'field_srs_id';
	$fields['term']['TERM_SHORT_DESC'] = 'title';
	$fields['term']['TERM_CODE'] = 'field_term_code';

	$fields['instructor']['INSTRUCTOR_ID'] = 'field_srs_id';
	$fields['instructor']['CLIENT_ID'] = 'title';
	$fields['instructor']['DESCRIPTION'] = 'field_srs_instructor_bio';
	$fields['instructor']['ACADEMIC_QUALIFICATIONS'] = 'field_srs_instructor_academic';

	//$fields['section']['EXCLUDE_FIELD'] = 'STATUS_CODE';
	//$fields['section']['EXCLUDE_FIELD_VALUE'] = 'O';
	$fields['section']['SECTION_TITLE'] = 'title';
	$fields['section']['SECTION_ID'] = 'field_srs_id';
	$fields['section']['_SECTION_TITLE'] = 'field_srs_section_display_title';
	$fields['section']['WEB_DESCRIPTION'] = 'field_srs_section_description';
	$fields['section']['DISTANCE_FLAG'] = 'field_srs_section_online';
	$fields['section']['CAMPUS_ID'] = 'field_srs_section_campus';
	$fields['section']['START_DATE'] = 'field_srs_section_start_date';
	$fields['section']['END_DATE'] = 'field_srs_section_end_date';
	$fields['section']['MAXIMUM_QTY'] = 'field_srs_section_max_quantity';
	$fields['section']['REMAINING_ALERT_TRIGGER'] = 'field_srs_section_trigger';
	$fields['section']['START_TIME'] = 'field_srs_section_start_time';
	$fields['section']['END_TIME'] = 'field_srs_section_end_time';
	$fields['section']['TOTAL_SESSIONS'] = 'field_srs_section_num_sessions';
	$fields['section']['TERM_ID'] = 'field_srs_section_term';
	$fields['section']['TIME_COMMENTS'] = 'field_srs_section_time_comments';
	$fields['section']['SECTION_CODE'] = 'field_srs_section_code';
	$fields['section']['CALENDAR_DESCRIPTION'] = 'field_srs_section_cal_desc';
	$fields['section']['MAXIMUM_WAITLIST_QTY'] = 'field_srs_section_max_waitlist';
	$fields['section']['REGISTER_BY_DATE'] = 'field_srs_section_reg_override';
	$fields['section']['STATUS_CODE'] = 'field_srs_status_code';
	$fields['section']['WEB_FLAG'] = 'field_srs_section_web_flag';

	$fields['dow']['DOW_ID'] = 'field_srs_id';
	$fields['dow']['DOW_SHORT_DESC'] = 'title';
	$fields['dow']['DOW_CODE'] = 'field_dow_day';

  return $fields;
}

