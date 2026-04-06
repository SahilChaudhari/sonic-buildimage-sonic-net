# {C} Copyright 2023 AMD Systems Inc. All rights reserved
#############################################################################
# Pensando
#
# Module contains an implementation of SONiC Platform Base API and
# provides the Chassis information which are available in the platform
#
#############################################################################

try:
    import os
    import threading
    import time
    import sys
    import subprocess
    from sonic_platform_base.chassis_base import ChassisBase
    from .helper import APIHelper
    import syslog
    import inspect
    from sonic_py_common import syslogger
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

NUM_THERMAL = 2
NUM_VOLTAGE_SENSORS = 6
NUM_CURRENT_SENSORS = 3
HOST_REBOOT_CAUSE_PATH = "/host/reboot-cause/"
REBOOT_CAUSE_FILE = "reboot-cause.txt"
HOST_CHK_CMD = "docker > /dev/null 2>&1"
REBOOT_CAUSE_SOFTWARE = "Software causes"
REBOOT_CAUSE_EXTERNAL = "External causes"
RESET_CAUSE_PATH = "/sys/firmware/pensando/rstcause/this_cause"
DOCKER_HWSKU_PATH = '/usr/share/sonic/platform'

#cpld masks for system led
SYSTEM_LED_GREEN   = 0x7
SYSTEM_LED_YELLOW  = 0x38
SYSTEM_LED_REG     = 0x15

SYSLOG_IDENTIFIER = "sonic_platform.chassis"
logger_instance = syslogger.SysLogger(SYSLOG_IDENTIFIER)

def log_info(msg, also_print_to_console=False):
    logger_instance.log_info(msg, also_print_to_console)

REBOOT_CAUSE_MAP = {
    25 : "ASIC warm reset",
    24 : "eSecure enabled",
    23 : "AC power cycle/CPLD reload",
    20 : "Host power cycle",
    16 : "Software power cycle",
    19 : "Therm trip",
    18 : "VRD fault",
    12 : "Runtime watchdog reset",
    8 : "Pcie reset",
    2 : "Pcie reset",
    3 : "NCSID power cycle",
    0 : "Reboot",
    1 : "Kernel Panic",
    15 : "Hardware watchdog reset",
}
REBOOT_CAUSE_NON_HARDWARE_LIST = [16, 20, 0, 1, 8, 2, 24, 25, 12]

class Chassis(ChassisBase):
    """Platform-specific Chassis class"""
    __shared_state = dict()
    _thermals_initialized = False
    _voltage_sensor_initialized = False
    _current_sensor_initialized = False

    def __init__(self):
        self.__dict__ = self.__shared_state
        self._api_helper = APIHelper()
        ChassisBase.__init__(self)

        log_info("System chassis is ready")

    ##############################################
    # System LED methods
    ##############################################

    def initizalize_system_led(self):
        '''Handled by sysmond in dpu container'''
        return True

    def get_status_led(self):
        return self.STATUS_LED_COLOR_GREEN

    def set_status_led(self, color):
        '''handled by sysmond in dpu container'''
        return False

    ##############################################
    # THERMAL methods
    ##############################################

    def get_num_thermals(self):
        if not self._thermals_initialized:
            self.__initialize_thermals()
        return len(self._thermal_list)

    def get_all_thermals(self):
        if not self._thermals_initialized:
            self.__initialize_thermals()
        return self._thermal_list

    def get_thermal(self, index):
        """
        Retrieves thermal unit represented by (0-based) index <index>
        Args:
            index: An integer, the index (0-based) of the thermal to
            retrieve
        Returns:
            An object dervied from ThermalBase representing the specified thermal
        """
        thermal = None

        try:
            if not self._thermals_initialized:
                self.__initialize_thermals()
            thermal = self._thermal_list[index]
        except IndexError:
            sys.stderr.write("THERMAL index {} out of range (0-{})\n".format(
                             index, len(self._thermal_list)-1))
        return thermal

    def __initialize_thermals(self):
        from sonic_platform.thermal import Thermal
        global NUM_THERMAL
        NUM_THERMAL = 0
        for index in range(0, NUM_THERMAL):
            thermal = Thermal(index)
            self._thermal_list.append(thermal)
        self._thermals_initialized = True
        log_info("System thermal sensors are initialized")

    ##############################################
    # Other methods
    ##############################################

    def get_eeprom(self):
        if not hasattr(self, "_eeprom") or self._eeprom is None:
                self.__initialize_eeprom()
        return self._eeprom

    def __initialize_eeprom(self):
        from sonic_platform.eeprom import Eeprom
        self._eeprom = Eeprom()
        log_info("system eeprom is ready")

    def get_watchdog(self):
        from sonic_platform.watchdog import Watchdog
        watchdog = Watchdog()
        log_info("Watchdog initialized")
        return watchdog

    ##############################################
    # device or chassis methods
    ##############################################

    def get_name(self):
        """
        Retrieves the name of the chassis
        Returns:
            string: The name of the chassis
        """
        model = ""
        hwsku = self._api_helper.get_hwsku()
        try:
            model = self.get_model()
        except:
            return hwsku
        return hwsku + " " + model

    def get_presence(self):
        """
        Retrieves the presence of the chassis
        Returns:
            bool: True if chassis is present, False if not
        """
        return True

    def get_model(self):
        """
        Retrieves the model number (or part number) of the chassis
        Returns:
            string: Model/part number of chassis
        """
        if not hasattr(self, "_eeprom") or self._eeprom is None:
            self.__initialize_eeprom()
        return self._eeprom.get_part_number()

    def get_serial(self):
        """
        Retrieves the serial number of the chassis (Service tag)
        Returns:
            string: Serial number of chassis
        """
        if not hasattr(self, "_eeprom") or self._eeprom is None:
            self.__initialize_eeprom()
        return self._eeprom.get_serial_number()

    def get_status(self):
        """
        Retrieves the operational status of the chassis
        Returns:
            bool: A boolean value, True if chassis is operating properly
            False if not
        """
        return True

    def get_base_mac(self):
        """
        Retrieves the base MAC address for the chassis
        Returns:
            A string containing the MAC address in the format
            'XX:XX:XX:XX:XX:XX'
        """
        if not hasattr(self, "_eeprom") or self._eeprom is None:
            self.__initialize_eeprom()
        return self._eeprom.get_base_mac()

    def get_system_eeprom_info(self):
        """
        Retrieves the full content of system EEPROM information for the chassis
        Returns:
            A dictionary where keys are the type code defined in
            OCP ONIE TlvInfo EEPROM format and values are their corresponding
            values.
        """
        if not hasattr(self, "_eeprom") or self._eeprom is None:
            self.__initialize_eeprom()
        return self._eeprom.system_eeprom_info()

    def get_my_slot(self):
        return 0

    def get_reboot_cause(self):
        """
        Retrieves the cause of the previous reboot
        Returns:
            A tuple (string, string) where the first element is a string
            containing the cause of the previous reboot. This string must be
            one of the predefined strings in this class. If the first string
            is "REBOOT_CAUSE_HARDWARE_OTHER", the second string can be used
            to pass a description of the reboot cause.
        """
        return (self.REBOOT_CAUSE_NON_HARDWARE, "Unknown")

    def get_position_in_parent(self):
        """
        Retrieves 1-based relative physical position in parent device. If the agent cannot determine the parent-relative position
        for some reason, or if the associated value of entPhysicalContainedIn is '0', then the value '-1' is returned
        Returns:
            integer: The 1-based relative physical position in parent device or -1 if cannot determine the position
        """
        return -1

    def is_replaceable(self):
        """
        Indicate whether this device is replaceable.
        Returns:
            bool: True if it is replaceable.
        """
        return False

