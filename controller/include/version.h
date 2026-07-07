#ifndef VERSION_H
#define VERSION_H

#include <string>

// Define firmware versions for master and slave
#define MASTER_FIRMWARE_VERSION "1.1.0"
#define SLAVE_FIRMWARE_VERSION "1.1.0"

/**
 * Get the current firmware version based on device role
 * @return Version string matching the device role (master or slave)
 */
std::string getFirmwareVersion();

/**
 * Get the master firmware version
 */
std::string getMasterVersion();

/**
 * Get the slave firmware version
 */
std::string getSlaveVersion();

#endif // VERSION_H
