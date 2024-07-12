/*
htop - linux/LibSensors.c
(C) 2020-2023 htop dev team
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "linux/LibSensors.h"

#ifdef HAVE_SENSORS_SENSORS_H

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sensors/sensors.h>

#include "Macros.h"
#include "XUtils.h"
#include "linux/LinuxMachine.h"
#define MAX_PATH 256
#define THERMAL_ZONE_PATH "/sys/devices/virtual/thermal/thermal_zone2/temp"

#ifdef BUILD_STATIC

#define sym_sensors_init sensors_init
#define sym_sensors_cleanup sensors_cleanup
#define sym_sensors_get_detected_chips sensors_get_detected_chips
#define sym_sensors_get_features sensors_get_features
#define sym_sensors_get_subfeature sensors_get_subfeature
#define sym_sensors_get_value sensors_get_value

#else

static int (*sym_sensors_init)(FILE*);
static void (*sym_sensors_cleanup)(void);
static const sensors_chip_name* (*sym_sensors_get_detected_chips)(const sensors_chip_name*, int*);
static const sensors_feature* (*sym_sensors_get_features)(const sensors_chip_name*, int*);
static const sensors_subfeature* (*sym_sensors_get_subfeature)(const sensors_chip_name*, const sensors_feature*, sensors_subfeature_type);
static int (*sym_sensors_get_value)(const sensors_chip_name*, int, double*);

static void* dlopenHandle = NULL;

#endif /* BUILD_STATIC */

int LibSensors_init(void) {
#ifdef BUILD_STATIC

   return sym_sensors_init(NULL);

#else

   if (!dlopenHandle) {
      /* Find the unversioned libsensors.so (symlink) and prefer that, but Debian has .so.5 and Fedora .so.4 without
         matching symlinks (unless people install the -dev packages) */
      dlopenHandle = dlopen("libsensors.so", RTLD_LAZY);
      if (!dlopenHandle)
         dlopenHandle = dlopen("libsensors.so.5", RTLD_LAZY);
      if (!dlopenHandle)
         dlopenHandle = dlopen("libsensors.so.4", RTLD_LAZY);
      if (!dlopenHandle)
         goto dlfailure;

      /* Clear any errors */
      dlerror();

      #define resolve(symbolname) do {                                      \
         *(void **)(&sym_##symbolname) = dlsym(dlopenHandle, #symbolname);  \
         if (!sym_##symbolname || dlerror() != NULL)                        \
            goto dlfailure;                                                 \
      } while(0)

      resolve(sensors_init);
      resolve(sensors_cleanup);
      resolve(sensors_get_detected_chips);
      resolve(sensors_get_features);
      resolve(sensors_get_subfeature);
      resolve(sensors_get_value);

      #undef resolve
   }

   return sym_sensors_init(NULL);


dlfailure:
   if (dlopenHandle) {
      dlclose(dlopenHandle);
      dlopenHandle = NULL;
   }
   return -1;

#endif /* BUILD_STATIC */
}

void LibSensors_cleanup(void) {
#ifdef BUILD_STATIC

   sym_sensors_cleanup();

#else

   if (dlopenHandle) {
      sym_sensors_cleanup();

      dlclose(dlopenHandle);
      dlopenHandle = NULL;
   }

#endif /* BUILD_STATIC */
}

int LibSensors_reload(void) {
#ifndef BUILD_STATIC
   if (!dlopenHandle) {
      errno = ENOTSUP;
      return -1;
   }
#endif /* !BUILD_STATIC */

   sym_sensors_cleanup();
   return sym_sensors_init(NULL);
}

static int tempDriverPriority(const sensors_chip_name* chip) {
   static const struct TempDriverDefs {
      const char* prefix;
      int priority;
   } tempDrivers[] =  {
      { "coretemp",    0 },
      { "via_cputemp", 0 },
      { "cpu_thermal", 0 },
      { "k10temp",     0 },
      { "zenpower",    0 },
      /* Low priority drivers */
      { "acpitz",      1 },
   };

   for (size_t i = 0; i < ARRAYSIZE(tempDrivers); i++)
      if (String_eq(chip->prefix, tempDrivers[i].prefix))
         return tempDrivers[i].priority;

   return -1;
}
static double readTemperatureFromFile(const char* filePath) {
    FILE* file = fopen(filePath, "r");
    if (!file) {
        perror("Failed to open temperature file");
        return NAN;
    }

    char buffer[16];
    if (!fgets(buffer, sizeof(buffer), file)) {
        perror("Failed to read temperature");
        fclose(file);
        return NAN;
    }
    fclose(file);

    return strtod(buffer, NULL) / 1000.0; // Convert to degrees Celsius
}
void LibSensors_getCPUTemperatures(CPUData* cpus, unsigned int existingCPUs, unsigned int activeCPUs) {
    assert(existingCPUs > 0 && existingCPUs < 16384);

    double* data = (double*)malloc((existingCPUs + 1) * sizeof(double));
    if (!data) {
        perror("Failed to allocate memory for temperature data");
        return;
    }

    for (size_t i = 0; i < existingCPUs + 1; i++)
        data[i] = NAN;

    double temp = readTemperatureFromFile(THERMAL_ZONE_PATH);

    // If the temperature is valid, set it for all CPUs
    if (!isnan(temp)) {
        for (size_t i = 0; i <= existingCPUs; i++)
            data[i] = temp;
    }

    for (size_t i = 0; i <= existingCPUs; i++)
        cpus[i].temperature = data[i];

    free(data);
}

#endif /* HAVE_SENSORS_SENSORS_H */
