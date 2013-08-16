<?php
/**
 * @file
 * User's loan lists.
 */

/**
 * Plugins are described by creating a $plugin array which will be used
 * by the system that includes this file.
 */
$plugin = array(
  'title' => t('User loan list'),
  'description' => t('The current loans for a user'),
  'single' => TRUE,
  'content_types' => array('list'),
  'required context' => new ctools_context_required(t('User'), 'user'),
  'category' => t('User'),
);

/**
 * Render the block.
 */
function ding_loan_loans_content_type_render($subtype, $conf, $panel_args, $context) {
  // Define basic block object.
  $block = new stdClass();
  $block->module = 'ding_loan';
  $block->delta = 'loans';
  $block->title = t('Loan list');
  $ill_enabled = ding_provider_implements('ill', 'list');
  $list_type = !empty($conf['loans_list_type']) ? $conf['loans_list_type'] : 'loan';

  // In case pane tries to show ILL list, but provider does not implement that,
  // skip this pane.
  if (!$ill_enabled && $list_type == 'ill') {
    return new stdClass();
  }

  // Get loans from the provider.
  $account = isset($context->data) ? $context->data : NULL;
  $loans = ding_provider_invoke_page($list_type, 'list', $account);

  // Preload all entities to speed up the page.
  $ids = array();
  foreach ($loans as $item) {
    $ids[] = $item->ding_entity_id;
  }
  ding_entity_load_multiple($ids);

  // Set block content, with loans form.
  if ($list_type == 'ill') {
    $block->content = ding_loan_get_table('ding_loan_loans_form', $loans);
  }
  else {
    $block->content = ding_provider_get_form('ding_loan_loans_form', $account, $loans);
  }

  return $block;
}

/**
 * Adding the admin form, to be able to control the required context.
 */
function ding_loan_loans_content_type_edit_form($form, $form_state) {
  $conf = $form_state['conf'];

  $form['loans_list_type'] = array(
    '#title' => t('List type'),
    '#type' => 'radios',
    '#options' => array(
      'loan' => t('Loans'),
      'ill' => t('Interlibrary loans'),
    ),
    '#default_value' => !empty($conf['loans_list_type']) ? $conf['loans_list_type'] : 'loan',
  );

  return $form;
}

/**
 * Custom submit handler for the pane settings form.
 *
 * @param array $form
 *   Form structure.
 * @param array $form_state
 *   Form state values.
 */
function ding_loan_loans_content_type_edit_form_submit($form, $form_state) {
  $form_state['conf']['loans_list_type'] = $form_state['values']['loans_list_type'];
}

/**
 * Implements a drupal form, which show a list of reservations.
 */
function ding_loan_loans_form($form, &$form_state, $account, $loans = array()) {
  $options = array();

  $header = array(
    'title' => t('Title'),
    'loan_date' => t('Loan date'),
    'expiry' => t('Return date'),
    'operations' => '',
  );

  // Sort loans base on exipre date.
  uasort($loans, 'ding_loan_sort_expiry');

  // Store account information in the form (on server site).
  $form['account'] = array(
    '#type' => 'value',
    '#value' => $account,
  );

  // Store the loan list in the form (on server site).
  $form['items'] = array(
    '#type' => 'value',
    '#value' => $loans,
  );

  foreach ($loans as $id => $item) {
    $item->creator = 'fake';
    $entity = $item->entity;

    $options[$item->id] = array(
      '#disabled' => $item->renewable,
      'title' => array(
        'data' => array(
          $entity ? ting_object_view($entity, 'user_list') : array('#markup' => $item->display_name),
        ),
        'class' => 'title',
      ),
      'loan_date' => array(
        'data' => format_date(strtotime(check_plain($item->loan_date)), 'custom', 'd/m/Y'),
        'class' => 'created-date',
      ),
      'expiry' => array(
        'data' => format_date(strtotime(check_plain($item->expiry)), 'custom', 'd/m/Y'),
        'class' => 'expire-date',
      ),
      'operations' => array(
        'data' => array(),
        'class' => 'operations',
      ),
    );

    // Add periodical number, if any.
    if (!empty($item->notes)) {
      $options[$item->id]['title']['data'][] = array(
        '#type' => 'markup',
        '#prefix' => '<p class="order-id periodical-number">',
        '#markup' => t('(Periodical no. @periodical)', array('@periodical' => check_plain($item->notes))),
        '#suffix' => '</p>',
      );
    }

    // Add material number.
    if (isset($item->materials_number)) {
      $options[$item->id]['title']['data'][] = array(
        '#type' => 'markup',
        '#prefix' => '<p class="order-id">',
        '#markup' => t('Material no. @mat_no', array('@mat_no' => $item->materials_number)),
        '#suffix' => '</p>',
      );
    }

    // Add renew load opreation.
    if (isset($item->renewable) && $item->renewable) {
      // Button name, needed for form API.
      $name = 'renew-' . $item->id;
      $options[$item->id]['operations']['data'][] = array(
        '#type' => 'submit',
        '#prefix' => '<div class="renew-loan-button">',
        '#value' => t('Renew'),
        '#submit' => array('ding_loan_loans_form_renew_submit'),
        '#loan_id' => $item->id,
        '#name' => $name,
        '#disabled' => !$item->renewable,
        '#suffix' => '</div>',
        '#attributes' => array(
          'class' => array(
            $item->renewable ? 'renewable' : 'not-renewable',
          ),
        ),
      );
    }
  }

  $form['loans'] = array(
    '#type' => 'fieldset',
    '#title' => t('Loans'),
  );

  $form['loans']['table'] = array(
    '#type' => 'tableselect_form',
    '#header' => $header,
    '#options' => $options,
    '#empty' => t('No loans'),
    '#process' => array('ding_base_element_process_tableselect_form', '_ding_loan_loans_disable'),
    '#weight' => 1,
  );

  $form['actions_bottom'] = array(
    '#weight' => 2,
  );
  $form['actions_bottom']['submit_second'] = array(
    '#type' => 'submit',
    '#prefix' => '<div class="renew-loan-button">',
    '#value' => t('Renew selected'),
    '#suffix' => '</div>',
  );

  return $form;
}


/**
 * Process function to disable checkboxes for unrenewable materials.
 */
function _ding_loan_loans_disable($element) {
  foreach ($element['#options'] as $key => $choice) {
    if (isset($choice['#disabled']) && !$choice['#disabled']) {
      $element[$key]['#disabled'] = TRUE;
      $element[$key]['#attributes']['title'] = t('Not renewable');
    }
  }
  return $element;
}

/**
 * Submit handler for the form.
 */
function ding_loan_loans_form_submit($form, &$form_state) {
  $ids = array();
  if (isset($form_state['values']['loans'])) {
    foreach (array_filter($form_state['values']['loans']) as $id) {
      // The loans array has _form_options array, henc the is_string check.
      if (is_string($id) && $form_state['values']['items'][$id]->renewable) {
        $ids[] = $id;
      }
    }
  }
  else {
    foreach ($form_state['complete form']['loans']['table']['#value'] as $id) {
      $ids[] = $id;
    }
  }

  $result = ding_provider_invoke('loan', 'renew', $form_state['values']['account'], $ids);
  foreach ($result as $id => $status) {
    if ($object = $form_state['values']['items'][$id]->entity) {
      $title = $object->title;
    }
    else {
      $title = $item->display_name;
    }
    // @todo Better feedback.
    switch ($status) {
      case DingProviderLoan::STATUS_RENEWED:
        drupal_set_message(t('"@title renewed"', array('@title' => $title)));
        break;

      case DingProviderLoan::STATUS_NOT_RENEWED:
        if ($status['message'] == 'maxNofRenewals') {
          $message = t('Maximum number of renewals reached');
        }
        elseif ($status['message'] == 'copyIsReserved') {
          $message = t('The material is reserved by another loaner');
        }
        else {
          $message = t('Unable to renew material');
        }
        drupal_set_message(t('"@title not renewed: @message"', array('@title' => $title, '@message' => $message)), 'warning');
        break;

      default:
        watchdog('ding_loan', 'Unknown renewal status code @code.', array('@code' => $status), WATCHDOG_ERROR);
    }
  }
}

/**
 * Renew form handler.
 */
function ding_loan_loans_form_renew_submit($form, &$form_state) {
  form_set_value($form['loans'], array($form_state['triggering_element']['#loan_id']), $form_state);
  ding_loan_loans_form_submit($form, $form_state);
}
