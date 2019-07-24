/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 I2C communications interface API */

#ifndef _I2C_API_H__
#define _I2C_API_H__

#include <stdint.h>
#include "common.h"
#include "ec_commands.h"
#include "i2c_regs.h"

/***** Definitions *****/

/**
 * typedef i2c_speed_t - I2C speed modes.
 * @I2C_STD_MODE: 100KHz bus speed
 * @I2C_FAST_MODE: 400KHz Bus Speed
 * @I2C_FASTPLUS_MODE: 1MHz   Bus Speed
 * @I2C_HS_MODE: 3.4MHz Bus Speed
 */
typedef enum {
	I2C_STD_MODE = 100000,
	I2C_FAST_MODE = 400000,
	I2C_FASTPLUS_MODE = 1000000,
	I2C_HS_MODE = 3400000
} i2c_speed_t;

/** 
 * typedef i2c_transfer_direction_t - I2C Transfer Direction.
 */
typedef enum {
	I2C_TRANSFER_DIRECTION_MASTER_WRITE = 0,
	I2C_TRANSFER_DIRECTION_MASTER_READ = 1,
	I2C_TRANSFER_DIRECTION_NONE = 2
} i2c_transfer_direction_t;

/** 
 * typedef i2c_autoflush_disable_t - Enable/Disable TXFIFO Autoflush mode.
 */
typedef enum {
	I2C_AUTOFLUSH_ENABLE = 0,
	I2C_AUTOFLUSH_DISABLE = 1
} i2c_autoflush_disable_t;

/** 
 * typedef i2c_master_state_t - Available transaction states for I2C Master.
 */
typedef enum {
	I2C_MASTER_IDLE = 1,
	I2C_MASTER_START = 2,
	I2C_MASTER_WRITE_COMPLETE = 3,
	I2C_MASTER_READ_COMPLETE = 4,
	I2C_MASTER_ERROR = EC_ERROR_UNKNOWN
} i2c_master_state_t;

/** 
 * typedef i2c_slave_state_t - Available transaction states for I2C Slave.
 */
typedef enum {
	I2C_SLAVE_ADDR_MATCH = 1,
	I2C_SLAVE_WRITE_COMPLETE = 2,
	I2C_SLAVE_READ_COMPLETE = 3,
	I2C_SLAVE_ERROR = EC_ERROR_UNKNOWN
} i2c_slave_state_t;

/** 
 * typedef i2c_req_t - I2C Transaction request.
 */
typedef struct i2c_req i2c_req_t;

/**
 * struct i2c_req - I2C Transaction request.
 * @addr: I2C 7-bit Address right aligned, bit 6 to bit 0.
 * 	  Only supports 7-bit addressing. LSb of the given
 * 	  address will be used as the read/write bit, the addr
 * 	  will not be shifted. Used for both master and slave
 * 	  transactions.
 * @tx_data: Data for mater write/slave read.
 * @rx_data: Data for master read/slave write.
 * @tx_len:  Length of tx data.
 * @rx_len:  Length of rx.
 * @tx_num:  Number of tx bytes sent.
 * @rx_num:  Number of rx bytes sent.
 * @direction: For the master, sets direction bit in address. 
 *             For the slave, direction of request from master.
 * @restart: Restart or stop bit indicator.
 *           0 to send a stop bit at the end of the transaction
 *           Non-zero to send a restart at end of the transaction
 *           Only used for Master transactions.
 * @sw_autoflush_disable: Enable/Disable autoflush.
 * @driver_status: Driver status to send to the host
 * @callback: Callback for asynchronous request.
 *            First argument is to the transaction request.
 *            Second argument is the error code.
 */
struct i2c_req {
	uint8_t addr; 
	const uint8_t *tx_data; 
	uint8_t *rx_data; 
	unsigned tx_len;
	unsigned rx_len; 
	unsigned tx_num; 
	unsigned rx_num; 
	i2c_transfer_direction_t direction;
	int restart; 
	i2c_autoflush_disable_t sw_autoflush_disable;
	enum ec_status driver_status;
	void (*callback)(i2c_req_t *, int);
};

/**
 * I2C_Api_Init() - Initialize and enable I2C.
 * @i2c:      Pointer to I2C peripheral registers.
 * @i2cspeed: Desired speed (I2C mode).
 * @sys_cfg:  System configuration object.
 * Return: EC_SUCCESS if successful, otherwise returns a common error code
 */
int I2C_Api_Init(mxc_i2c_regs_t *i2c, i2c_speed_t i2cspeed);

/**
 * I2C_Api_MasterWrite()
 * @i2c:  Pointer to I2C regs.
 * @addr: I2C 7-bit Address left aligned, bit 7 to bit 1.
 *        Only supports 7-bit addressing. LSb of the given address
 *        will be used as the read/write bit, the \p addr <b>will
 *        not be shifted. Used for both master and
 *        slave transactions.
 * @data: Data to be written.
 * @len:  Number of bytes to Write.
 * @restart: 0 to send a stop bit at the end of the transaction,
 *	     otherwise send a restart.
 *
 * Will block until transaction is complete. 
 *
 * Return:   EC_SUCCESS if successful, otherwise returns a common error code
 */
int I2C_Api_MasterWrite(mxc_i2c_regs_t *i2c, uint8_t addr, int start, int stop,
			const uint8_t *data, int len, int restart);

/**
 * I2C_Api_MasterRead()
 * @i2c        Pointer to I2C regs.
 * @addr       I2C 7-bit Address right aligned, bit 6 to bit 0.
 * @data       Data to be written.
 * @len        Number of bytes to Write.
 * @restart    0 to send a stop bit at the end of the transaction,
 * 	       otherwise send a restart.
 *
 * Will block until transaction is complete. 
 *
 * Return:     EC_SUCCESS if successful, otherwise returns a common error code
 */
int I2C_Api_MasterRead(mxc_i2c_regs_t *i2c, uint8_t addr, int start, int stop,
		       uint8_t *data, int len, int restart);

/**
 * I2C_Api_SlaveAsync() - Slave Read and Write Asynchronous.
 * @i2c:   Pointer to I2C regs.
 * @req:   Request for an I2C transaction.
 * Return: EC_SUCCESS if successful, otherwise returns a common error code
 */
int I2C_Api_SlaveAsync(mxc_i2c_regs_t *i2c, i2c_req_t *req);

/**
 * I2C_Api_Handler() - I2C interrupt handler.
 * @i2c: Base address of the I2C module.
 * 
 * This function should be called by the application from the interrupt
 * handler if I2C interrupts are enabled. Alternately, this function
 * can be periodically called by the application if I2C interrupts are
 * disabled.
 */
void I2C_Api_Handler(mxc_i2c_regs_t *i2c);

/**
 * I2C_Api_DrainTX()
 * @i2c: Pointer to I2C regs.
 * 
 * Drain all of the data in the TXFIFO.
 */
void I2C_Api_DrainTX(mxc_i2c_regs_t *i2c);

#endif /* _I2C_API_H__ */
