<?php
/**
 * @file
 * Implementation of classes from the batch ingest framework.
 */

$path = drupal_get_path('module', 'islandora_audio_batch');
include $path . '/includes/utilities.inc';
/**
 * Batch preprocessor class.
 */
class IslandoraAudioBatchPreprocessor extends IslandoraBatchPreprocessor {
  // Prefixed flag for outputing echo or print statements.
  protected $iacpVerbose = FALSE;
  /**
   * Function to get the OBJ XML files from the input directory.
   */
  public function preprocess() {

    if (strtolower($this->parameters['iacp_verbose']) == 'true') {
      $this->iacpVerbose = TRUE;
    }

    // Accumulate a list of the objects added to the queue.
    $added = array();
    
    // Keep track of PIDS added -- child PID can only be added once to database.
    $added_pids = array();

    $input_path = $this->parameters['target'];
    // Add batchset_id property for use later in ingest phase in object.inc
    // $batch_object->batchset_id = $this->getSetId();
    $this->parameters['set_id'] = $this->getSetId();

    $dir = new RecursiveDirectoryIterator($input_path);
    $iterator = new RecursiveIteratorIterator($dir);

    // Audio batch utilities class.
    $cb_utilities = new Utilities();

    $files = array();
    foreach ($iterator as $fileinfo) {
      $file_path = $fileinfo->getPathname();
      $file_extension = pathinfo($file_path, PATHINFO_EXTENSION);
      $file_name_with_extension = basename($file_path);
      $is_obj_file = $cb_utilities->extInContentModelMap($file_extension);
      $parts = explode('/', $file_path);
      $pid = $this->parameters['namespace'] . ':' . str_replace("batch_", "", $parts[(count($parts) - 2)]);
      if ((array_search($pid, $added_pids) === FALSE)) {
        // this object's pid will need to be set because the children override the value in 
        // the top half of this logic -- and the parent compound object will need to have 
        // that value assigned again before adding to database.
        $this->parameters['assign_pid'] = $pid;

        if ($fileinfo->isFile() && $is_obj_file) {
          $added_pids[] = $pid;
          $this->parameters['content_models'] = $this->get_model_from_filename($file_name_with_extension);
          $batch_object = new IslandoraCompoundBatchObject($this->connection, $file_path, $this->parameters);
          // Add the instances to the Islandora batch queue.
          $this->addToDatabase($batch_object);
          $added[] = $batch_object;
        }
        if ($file_name_with_extension == 'structure.xml') {
          $added_pids[] = $pid;
          // The structure file will be in the root directory of the compound
          // object.  We need to create a parent wrapper object to associate
          // the OBJ child objects too.  Along with the structure file in the
          // root directory of the compound object there should be a MODS file
          // (or other metadata file) giving information about the object.
          // Associate this with the batch object.
          $parent_mods_file_path = dirname($file_path) . DIRECTORY_SEPARATOR . 'MODS.xml';
          $batch_object = new IslandoraCompoundBatchObject($this->connection, $parent_mods_file_path, $this->parameters);
          // Parent wrapper object must be compound.
          $batch_object->models = 'islandora:compoundCmodel';
          // Ad the instance to the Islandora batch queue.
          // Returns PID for parent object.
          $compound_object_pid = $this->addToDatabase($batch_object);
          $this->addStructureData($file_path, $compound_object_pid);
        }
      }

    }

    return $added;
  }
  
  /**
  * To get the content_models value for the child object based on the filename.
  *
  * @param type $filename
  */
  public function get_model_from_filename($filename) {
    return 'islandora:sp_audioCModel';
  }

  /**
   * Echos statement if iacpVerbose property is true.
   *
   * Set using --iacp_verbose drush option.
   */
  public function iacpecho($string) {
    if ($this->iacpVerbose == TRUE) {
      echo $string;
    }
  }

  /**
   * Parses the structure.xml file and adds data to the module's database table.
   */
  public function addStructureData($structure_file_path, $audio_object_pid) {
    static $object_num = 0;

    $batch_id = $this->getSetId();

    // Object ID is the batch set ID concatenated with the object number.
    $object_id = $batch_id . $object_num;

    // Increment.
    $object_num++;

    $doc = new DOMDocument();
    $doc->load($structure_file_path);

    /*
    <islandora_audio_object title="895">
    <child content="893"/>
    <child content="894"/>
    </islandora_audio_object>
     */

    $wanted_tags = array('child');
    foreach ($doc->getElementsByTagName('*') as $node) {
      if (in_array($node->nodeName, $wanted_tags)) {
        $this->iacpecho("Node name:\t" . $node->nodeName . "\n");
        if ($node->nodeName == 'child') {
          $xpath = new DOMXPath($doc);
          $child_contents = $xpath->query('@content', $node);
          $child_content = $child_contents->item(0);
          $this->iacpecho("Child content value:\t" . $child_content->nodeValue . "\n");
          $child_content_value = $child_content->nodeValue;
        }
        $path = $node->getNodePath();
        $this->iacpecho("Path:\t\t$path\n");

        $parent_node = $node->parentNode;
        $this->iacpecho("Parent path:\t" . $parent_node->getNodePath() . "\n\n");

        $parent_node = trim($parent_node->getNodePath());
        $insert_result = db_insert('islandora_compound_batch')
                    ->fields(array(
                        'object_id' => $object_id,
                        'object_xpath' => $path,
                        'parent_xpath' => $parent_node,
                        'child_content_value' => $child_content_value,
                        'parent_pid' => $audio_object_pid,
                        'batch_id' => $batch_id,
               ))->execute();

        $this->iacpecho("Insert Result: $insert_result" . PHP_EOL);
      }

    }

  } // End of addStructureData

}
