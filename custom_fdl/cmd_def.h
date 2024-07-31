#ifndef CMD_DEF_H
#define CMD_DEF_H

typedef enum dl_cmd_type {
	BSL_CMD_CONNECT              = 0x00,
	BSL_CMD_START_DATA           = 0x01,
	BSL_CMD_MIDST_DATA           = 0x02,
	BSL_CMD_END_DATA             = 0x03,
	BSL_CMD_EXEC_DATA            = 0x04,
 	BSL_CMD_NORMAL_RESET         = 0x05,
	BSL_CMD_READ_FLASH           = 0x06,
	BSL_CMD_CHANGE_BAUD          = 0x09,
	BSL_CMD_ERASE_FLASH          = 0x0A,
	BSL_CMD_READ_START           = 0x10,
	BSL_CMD_READ_MIDST           = 0x11,
	BSL_CMD_READ_END             = 0x12,
	BSL_CMD_OFF_CHG              = 0x13,

	/* response from the phone */
	BSL_REP_ACK                  = 0x80, 
	BSL_REP_VER                  = 0x81,
	BSL_REP_INVALID_CMD          = 0x82,
	BSL_REP_UNKNOWN_CMD          = 0x83,
	BSL_REP_OPERATION_FAILED     = 0x84,
	BSL_REP_NOT_SUPPORT_BAUDRATE = 0x85,

	/* Data Download */ 
	BSL_REP_DOWN_NOT_START       = 0x86,
	BSL_REP_DOWN_MULTI_START     = 0x87,
	BSL_REP_DOWN_EARLY_END       = 0x88,
	BSL_REP_DOWN_DEST_ERROR      = 0x89,
	BSL_REP_DOWN_SIZE_ERROR      = 0x8A,
	BSL_REP_VERIFY_ERROR         = 0x8B,
	BSL_REP_NOT_VERIFY           = 0x8C,

	BSL_PHONE_NOT_ENOUGH_MEMORY  = 0x8D,
	BSL_PHONE_WAIT_INPUT_TIMEOUT = 0x8E,
	BSL_PHONE_SUCCEED            = 0x8F,
	BSL_PHONE_VALID_BAUDRATE     = 0x90,
	BSL_PHONE_REPEAT_CONTINUE    = 0x91,
	BSL_PHONE_REPEAT_BREAK       = 0x92,

	BSL_REP_READ_FLASH           = 0x93,
	BSL_REP_READ_CHIP_TYPE       = 0x94,
	BSL_REP_READ_NVITEM          = 0x95,

	BSL_REP_LOG                  = 0xFF,
} dl_cmd_type_t;

#endif // CMD_DEF_H

