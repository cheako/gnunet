/*
     This file is part of GNUnet.
     (C)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file sensor/sensor_util_lib.c
 * @brief senor utilities
 * @author Omar Tarabai
 */
#include <inttypes.h>
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_sensor_util_lib.h"

#define LOG(kind,...) GNUNET_log_from (kind, "sensor-util",__VA_ARGS__)

/**
 * Minimum sensor execution interval (in seconds)
 */
#define MIN_INTERVAL 30

/**
 * Supported sources of sensor information
 */
static const char *sources[] = { "gnunet-statistics", "process", NULL };

/**
 * Supported datatypes of sensor information
 */
static const char *datatypes[] = { "numeric", "string", NULL };

/**
 * Parses a version number string into major and minor
 *
 * @param version full version string
 * @param major pointer to parsed major value
 * @param minor pointer to parsed minor value
 * @return #GNUNET_OK if parsing went ok, #GNUNET_SYSERROR in case of error
 */
static int
version_parse(char *version, uint16_t *major, uint16_t *minor)
{
  int majorval = 0;
  int minorval = 0;

  for(; isdigit(*version); version++)
  {
    majorval *= 10;
    majorval += *version - '0';
  }
  if(*version != '.')
    return GNUNET_SYSERR;
  version++;
  for(; isdigit(*version); version++)
  {
    minorval *= 10;
    minorval += *version - '0';
  }
  if(*version != 0)
    return GNUNET_SYSERR;
  *major = majorval;
  *minor = minorval;

  return GNUNET_OK;
}

/**
 * Load sensor definition from configuration
 *
 * @param cfg configuration handle
 * @param sectionname configuration section containing definition
 */
static struct SensorInfo *
load_sensor_from_cfg(struct GNUNET_CONFIGURATION_Handle *cfg, const char *sectionname)
{
  struct SensorInfo *sensor;
  char *version_str;
  char *starttime_str;
  char *endtime_str;
  unsigned long long time_sec;
  char *dummy;
  struct GNUNET_CRYPTO_EddsaPublicKey public_key;

  sensor = GNUNET_new(struct SensorInfo);
  //name
  sensor->name = GNUNET_strdup(sectionname);
  //version
  if(GNUNET_OK != GNUNET_CONFIGURATION_get_value_string(cfg, sectionname, "VERSION", &version_str))
  {
    LOG (GNUNET_ERROR_TYPE_ERROR, _("Error reading sensor version\n"));
    GNUNET_free(sensor);
    return NULL;
  }
  if(GNUNET_OK != version_parse(version_str, &(sensor->version_major), &(sensor->version_minor)))
  {
    LOG (GNUNET_ERROR_TYPE_ERROR, _("Invalid sensor version number, format should be major.minor\n"));
    GNUNET_free(sensor);
    GNUNET_free(version_str);
    return NULL;
  }
  GNUNET_free(version_str);
  //description
  GNUNET_CONFIGURATION_get_value_string(cfg, sectionname, "DESCRIPTION", &sensor->description);
  //category
  if(GNUNET_OK != GNUNET_CONFIGURATION_get_value_string(cfg, sectionname, "CATEGORY", &sensor->category) ||
        NULL == sensor->category)
  {
    LOG (GNUNET_ERROR_TYPE_ERROR, _("Error reading sensor category\n"));
    GNUNET_free(sensor);
    return NULL;
  }
  //enabled
  if(GNUNET_NO == GNUNET_CONFIGURATION_get_value_yesno(cfg, sectionname, "ENABLED"))
    sensor->enabled = GNUNET_NO;
  else
    sensor->enabled = GNUNET_YES;
  //start time
  sensor->start_time = NULL;
  if(GNUNET_OK == GNUNET_CONFIGURATION_get_value_string(cfg, sectionname, "START_TIME", &starttime_str))
  {
    GNUNET_STRINGS_fancy_time_to_absolute(starttime_str, sensor->start_time);
    LOG (GNUNET_ERROR_TYPE_DEBUG, "Start time loaded: `%s'. Parsed: %d\n", starttime_str, (NULL != sensor->start_time));
    GNUNET_free(starttime_str);
  }
  //end time
  sensor->end_time = NULL;
  if(GNUNET_OK == GNUNET_CONFIGURATION_get_value_string(cfg, sectionname, "END_TIME", &endtime_str))
  {
    GNUNET_STRINGS_fancy_time_to_absolute(endtime_str, sensor->end_time);
    LOG (GNUNET_ERROR_TYPE_DEBUG, "End time loaded: `%s'. Parsed: %d\n", endtime_str, (NULL != sensor->end_time));
    GNUNET_free(endtime_str);
  }
  //interval
  if(GNUNET_OK != GNUNET_CONFIGURATION_get_value_number(cfg, sectionname, "INTERVAL", &time_sec))
  {
    LOG (GNUNET_ERROR_TYPE_ERROR, _("Error reading sensor run interval\n"));
    GNUNET_free(sensor);
    return NULL;
  }
  if(time_sec < MIN_INTERVAL)
  {
    LOG (GNUNET_ERROR_TYPE_ERROR, _("Sensor run interval too low (%" PRIu64 " < %d)\n"),
        time_sec, MIN_INTERVAL);
    GNUNET_free(sensor);
    return NULL;
  }
  sensor->interval = GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, time_sec);
  //lifetime
  if(GNUNET_OK == GNUNET_CONFIGURATION_get_value_number(cfg, sectionname, "LIFETIME", &time_sec))
  {
    sensor->lifetime = GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, time_sec);
    if (sensor->lifetime.rel_value_us < sensor->interval.rel_value_us)
      LOG  (GNUNET_ERROR_TYPE_WARNING,
          "Lifetime of sensor data is preferred to be higher than interval for sensor `%s'.\n",
          sensor->name);
  }
  else
    sensor->lifetime = sensor->interval;
  //capabilities TODO
  //source
  if(GNUNET_OK != GNUNET_CONFIGURATION_get_value_choice(cfg, sectionname, "SOURCE", sources, (const char **)&sensor->source))
  {
    LOG (GNUNET_ERROR_TYPE_ERROR, _("Error reading sensor source\n"));
    GNUNET_free(sensor);
    return NULL;
  }
  if(sources[0] == sensor->source) //gnunet-statistics
  {
    if(GNUNET_OK != GNUNET_CONFIGURATION_get_value_string(cfg, sectionname, "GNUNET_STAT_SERVICE", &sensor->gnunet_stat_service) ||
        GNUNET_OK != GNUNET_CONFIGURATION_get_value_string(cfg, sectionname, "GNUNET_STAT_NAME", &sensor->gnunet_stat_name))
    {
      LOG (GNUNET_ERROR_TYPE_ERROR, _("Error reading sensor gnunet-statistics source information\n"));
      GNUNET_free(sensor);
      return NULL;
    }
    sensor->gnunet_stat_get_handle = NULL;
  }
  else if(sources[1] == sensor->source) //process
  {
    if(GNUNET_OK != GNUNET_CONFIGURATION_get_value_string(cfg, sectionname, "EXT_PROCESS", &sensor->ext_process))
    {
      LOG (GNUNET_ERROR_TYPE_ERROR, _("Error reading sensor process name\n"));
      GNUNET_free(sensor);
      return NULL;
    }
    GNUNET_CONFIGURATION_get_value_string(cfg, sectionname, "EXT_ARGS", &sensor->ext_args);
  }
  //expected datatype
  if(GNUNET_OK != GNUNET_CONFIGURATION_get_value_choice(cfg, sectionname, "EXPECTED_DATATYPE", datatypes, (const char **)&sensor->expected_datatype))
  {
    LOG (GNUNET_ERROR_TYPE_ERROR, _("Error reading sensor expected datatype\n"));
    GNUNET_free(sensor);
    return NULL;
  }
  if(sources[0] == sensor->source && datatypes[0] != sensor->expected_datatype)
  {
    LOG (GNUNET_ERROR_TYPE_ERROR, _("Invalid expected datatype, gnunet-statistics returns uint64 values\n"));
    GNUNET_free(sensor);
    return NULL;
  }
  //reporting mechanism
  if (GNUNET_OK == GNUNET_CONFIGURATION_get_value_string(cfg, sectionname, "COLLECTION_POINT", &dummy))
  {
    if(GNUNET_OK != GNUNET_CONFIGURATION_get_value_number(cfg, sectionname, "COLLECTION_INTERVAL", &time_sec))
    {
      LOG (GNUNET_ERROR_TYPE_ERROR, _("Error reading sensor collection interval\n"));
    }
    else
    {
      sensor->collection_interval = GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, time_sec);
      if (GNUNET_OK == GNUNET_CRYPTO_eddsa_public_key_from_string(dummy, strlen(dummy), &public_key))
      {
        sensor->collection_point = GNUNET_new(struct GNUNET_PeerIdentity);
        sensor->collection_point->public_key = public_key;
      }
    }
  }
  sensor->p2p_report = GNUNET_NO;
  if (GNUNET_YES == GNUNET_CONFIGURATION_get_value_yesno(cfg, sectionname, "P2P_REPORT"))
  {
    if(GNUNET_OK != GNUNET_CONFIGURATION_get_value_number(cfg, sectionname, "P2P_INTERVAL", &time_sec))
    {
      LOG (GNUNET_ERROR_TYPE_ERROR, _("Error reading sensor p2p reporting interval\n"));
    }
    else
    {
      sensor->p2p_interval = GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, time_sec);
      sensor->p2p_report = GNUNET_YES;
    }
  }
  //execution task
  sensor->execution_task = GNUNET_SCHEDULER_NO_TASK;
  //running
  sensor->running = GNUNET_NO;

  return sensor;
}

/**
 * Load sensor definition from file
 *
 * @param filename full path to file containing sensor definition
 */
static struct SensorInfo *
load_sensor_from_file(const char *filename)
{
  struct GNUNET_CONFIGURATION_Handle *sensorcfg;
  const char *filebasename;
  struct SensorInfo *sensor;

  //test file
  if(GNUNET_YES != GNUNET_DISK_file_test(filename))
  {
    LOG (GNUNET_ERROR_TYPE_ERROR, _("Failed to access sensor file: %s\n"), filename);
    return NULL;
  }
  //load file as configuration
  sensorcfg = GNUNET_CONFIGURATION_create();
  if(GNUNET_SYSERR == GNUNET_CONFIGURATION_parse(sensorcfg, filename))
  {
    GNUNET_CONFIGURATION_destroy(sensorcfg);
    LOG (GNUNET_ERROR_TYPE_ERROR, _("Failed to load sensor definition: %s\n"), filename);
    return NULL;
  }
  //configuration section should be the same as filename
  filebasename = GNUNET_STRINGS_get_short_name(filename);
  sensor = load_sensor_from_cfg(sensorcfg, filebasename);
  if(NULL == sensor)
  {
    GNUNET_CONFIGURATION_destroy(sensorcfg);
    return NULL;
  }
  sensor->def_file = GNUNET_strdup(filename);
  sensor->cfg = sensorcfg;

  return sensor;
}

/**
 * Compares version numbers of two sensors
 *
 * @param s1 first sensor
 * @param s2 second sensor
 * @return 1: s1 > s2, 0: s1 == s2, -1: s1 < s2
 */
static int
sensor_version_compare(struct SensorInfo *s1, struct SensorInfo *s2)
{
  if(s1->version_major == s2->version_major)
    return (s1->version_minor < s2->version_minor) ? -1 : (s1->version_minor > s2->version_minor);
  else
    return (s1->version_major < s2->version_major) ? -1 : (s1->version_major > s2->version_major);
}

/**
 * Adds a new sensor to given hashmap.
 * If the same name exist, compares versions and update if old.
 *
 * @param sensor Sensor structure to add
 * @param map Hashmap to add to
 * @return #GNUNET_YES if added, #GNUNET_NO if not added which is not necessarily an error
 */
static int
add_sensor_to_hashmap(struct SensorInfo *sensor, struct GNUNET_CONTAINER_MultiHashMap *map)
{
  struct GNUNET_HashCode key;
  struct SensorInfo *existing;

  GNUNET_CRYPTO_hash(sensor->name, strlen(sensor->name), &key);
  existing = GNUNET_CONTAINER_multihashmap_get(map, &key);
  if(NULL != existing) //sensor with same name already exists
  {
    if(sensor_version_compare(existing, sensor) >= 0) //same or newer version already exist
    {
      LOG (GNUNET_ERROR_TYPE_INFO, _("Sensor `%s' already exists with same or newer version\n"), sensor->name);
      return GNUNET_NO;
    }
    else
    {
      GNUNET_CONTAINER_multihashmap_remove(map, &key, existing); //remove the old version
      GNUNET_free(existing);
      LOG (GNUNET_ERROR_TYPE_INFO, "Upgrading sensor `%s' to a newer version\n", sensor->name);
    }
  }
  if(GNUNET_SYSERR == GNUNET_CONTAINER_multihashmap_put(map, &key, sensor, GNUNET_CONTAINER_MULTIHASHMAPOPTION_UNIQUE_ONLY))
  {
    LOG (GNUNET_ERROR_TYPE_ERROR, _("Error adding new sensor `%s' to global hashmap, this should not happen\n"), sensor->name);
    return GNUNET_NO;
  }

  return GNUNET_YES;
}

/**
 * Iterating over files in sensors directory
 *
 * @param cls closure
 * @param filename complete filename (absolute path)
 * @return #GNUNET_OK to continue to iterate
 */
static int
reload_sensors_dir_cb(void *cls, const char *filename)
{
  struct GNUNET_CONTAINER_MultiHashMap *sensors = cls;
  struct SensorInfo *sensor;

  if(GNUNET_YES != GNUNET_DISK_file_test(filename))
    return GNUNET_OK;
  sensor = load_sensor_from_file(filename);
  if(NULL == sensor)
  {
    LOG (GNUNET_ERROR_TYPE_ERROR,
        _("Error loading sensor from file: %s\n"), filename);
    return GNUNET_OK;
  }
  if(GNUNET_YES == add_sensor_to_hashmap(sensor, sensors))
    LOG (GNUNET_ERROR_TYPE_DEBUG,
        "Sensor `%s' added to global hashmap\n", sensor->name);
  else
    LOG (GNUNET_ERROR_TYPE_WARNING,
        "Could not add sensor `%s' to global hashmap\n", sensor->name);

  return GNUNET_OK;
}

/*
 * Get path to the directory containing the sensor definition files
 *
 * @return sensor files directory
 */
char *
GNUNET_SENSOR_get_sensor_dir ()
{
  char* datadir;
  char* sensordir;

  datadir = GNUNET_OS_installation_get_path(GNUNET_OS_IPK_DATADIR);
  GNUNET_asprintf(&sensordir, "%ssensors%s",
      datadir, DIR_SEPARATOR_STR);
  GNUNET_free(datadir);

  return sensordir;
}

/**
 * Reads sensor definitions from local data files
 *
 * @return a multihashmap of loaded sensors
 */
struct GNUNET_CONTAINER_MultiHashMap *
GNUNET_SENSOR_load_all_sensors ()
{
  char* sensordir;
  struct GNUNET_CONTAINER_MultiHashMap *sensors;

  sensors = GNUNET_CONTAINER_multihashmap_create(10, GNUNET_NO);
  sensordir = GNUNET_SENSOR_get_sensor_dir ();
  LOG (GNUNET_ERROR_TYPE_INFO,
      "Loading sensor definitions from directory `%s'\n", sensordir);
  GNUNET_assert(GNUNET_YES == GNUNET_DISK_directory_test(sensordir, GNUNET_YES));

  //read all files in sensors directory
  GNUNET_DISK_directory_scan(sensordir, &reload_sensors_dir_cb, sensors);
  LOG (GNUNET_ERROR_TYPE_INFO, "Loaded %d sensors from directory `%s'\n",
      GNUNET_CONTAINER_multihashmap_size(sensors), sensordir);
  GNUNET_free(sensordir);
  return sensors;
}