/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 I2C API for Chrome EC */

#include <stddef.h>
#include <stdint.h>
#include "registers.h"
#include "i2c_api.h"
#include "system.h"

#define I2C_SEND_BYTE_EC

/* **** Definitions **** */
#define I2C_ERROR                                                        \
	(MXC_F_I2C_INT_FL0_ARB_ER | MXC_F_I2C_INT_FL0_TO_ER |            \
	 MXC_F_I2C_INT_FL0_ADDR_NACK_ER | MXC_F_I2C_INT_FL0_DATA_ER |    \
	 MXC_F_I2C_INT_FL0_DO_NOT_RESP_ER | MXC_F_I2C_INT_FL0_START_ER | \
	 MXC_F_I2C_INT_FL0_STOP_ER)

#define T_LOW_MIN (160) /* tLOW minimum in nanoseconds */
#define T_HIGH_MIN (60) /* tHIGH minimum in nanoseconds */
#define T_R_MAX_HS (40) /* tR maximum for high speed mode in nanoseconds */
#define T_F_MAX_HS (40) /* tF maximum for high speed mode in nanoseconds */
#define T_AF_MIN (10) 	/* tAF minimun in nanoseconds */

/**
 * typedef i2c_req_state_t - Saves the state of the non-blocking requests
 * @req:
 * @master_state:
 * @slave_state:
 * @num_wr: Keep track of number of bytes loaded in the fifo during slave 
 * 			transmit.
 */
typedef struct {
	i2c_req_t *req;
	i2c_master_state_t master_state;
	i2c_slave_state_t slave_state;
	uint8_t num_wr;
} i2c_req_state_t;

static i2c_req_state_t states[MXC_I2C_INSTANCES];
static int slave_rx_remain = 0, slave_tx_remain = 0;

/* **** Function Prototypes **** */
static void I2C_Api_FreeCallback(int i2c_num, int error);
static void I2C_Api_ClearInterrupts(mxc_i2c_regs_t *i2c);

int SYS_I2C_Shutdown(mxc_i2c_regs_t *i2c)
{
	return EC_SUCCESS;
}

static int I2C_Api_Setspeed(mxc_i2c_regs_t *i2c, i2c_speed_t i2cspeed)
{
	uint32_t ticks;
	uint32_t ticks_lo;
	uint32_t ticks_hi;
	uint32_t time_pclk;
	uint32_t target_bus_freq;
	uint32_t time_scl_min;
	uint32_t clock_low_min;
	uint32_t clock_high_min;
	uint32_t clock_min;

	if (i2cspeed == I2C_HS_MODE) {
		/* Compute dividers for high speed mode. */
		time_pclk = 1000000 / (PeripheralClock / 1000);

		target_bus_freq = i2cspeed;
		if (target_bus_freq < 1000) {
			return EC_ERROR_INVAL;
		}

		time_scl_min = 1000000 / (target_bus_freq / 1000);
		clock_low_min = ((T_LOW_MIN + T_F_MAX_HS + (time_pclk - 1) - T_AF_MIN) /
			  time_pclk) -
			 1;
		clock_high_min = ((T_HIGH_MIN + T_R_MAX_HS + (time_pclk - 1) - T_AF_MIN) /
			  time_pclk) -
			 1;
		clock_min = ((time_scl_min + (time_pclk - 1)) / time_pclk) - 2;

		ticks_lo = (clock_low_min > (clock_min - clock_high_min)) ?
				   (clock_low_min) :
				   (clock_min - clock_high_min);
		ticks_hi = clock_high_min;

		if ((ticks_lo > (MXC_F_I2C_HS_CLK_HS_CLK_LO >>
				 MXC_F_I2C_HS_CLK_HS_CLK_LO_POS)) ||
		    (ticks_hi > (MXC_F_I2C_HS_CLK_HS_CLK_HI >>
				 MXC_F_I2C_HS_CLK_HS_CLK_HI_POS))) {
			return EC_ERROR_INVAL;
		}

		/* Write results to destination registers. */
		i2c->hs_clk = (ticks_lo << MXC_F_I2C_HS_CLK_HS_CLK_LO_POS) |
			      (ticks_hi << MXC_F_I2C_HS_CLK_HS_CLK_HI_POS);

		/* Still need to load dividers for the preamble that each
		 * high-speed transaction starts with. Switch setting to fast
		 * mode and fall out of if statement. 
		 */
		i2cspeed = I2C_FAST_MODE;
	}

	/* Get the number of periph clocks needed to achieve selected speed. */
	ticks = PeripheralClock / i2cspeed;

	/* For a 50% duty cycle, half the ticks will be spent high and half will
	 * be low. 
	 */
	ticks_hi = (ticks >> 1) - 1;
	ticks_lo = (ticks >> 1) - 1;

	/* Account for rounding error in odd tick counts. */
	if (ticks & 1) {
		ticks_hi++;
	}

	/* Will results fit into 9 bit registers?  (ticks_hi will always be >=
	 * ticks_lo.  No need to check ticks_lo.) 
	 */
	if (ticks_hi > 0x1FF) {
		return EC_ERROR_INVAL;
	}

	/* 0 is an invalid value for the destination registers. (ticks_hi will
	 * always be >= ticks_lo.  No need to check ticks_hi.) 
	 */
	if (ticks_lo == 0) {
		return EC_ERROR_INVAL;
	}

	/* Write results to destination registers. */
	i2c->clk_lo = ticks_lo;
	i2c->clk_hi = ticks_hi;

	return EC_SUCCESS;
}

int I2C_Api_Init(mxc_i2c_regs_t *i2c, i2c_speed_t i2cspeed)
{
	int idx = MXC_I2C_GET_IDX(i2c);
	/** 
	 * Always disable the HW autoflush on data NACK and let the SW handle
	 * the flushing.
	 */
	i2c->tx_ctrl0 |= 0x20;

	states[idx].num_wr = 0;

	i2c->ctrl = 0; /* clear configuration bits */
	i2c->ctrl = MXC_F_I2C_CTRL_I2C_EN; /* Enable I2C */
	i2c->master_ctrl = 0; /* clear master configuration bits */
	i2c->status = 0; /* clear status bits */

	i2c->ctrl = 0; /* clear configuration bits */
	i2c->ctrl = MXC_F_I2C_CTRL_I2C_EN; /* Enable I2C */
	i2c->master_ctrl = 0; /* clear master configuration bits */
	i2c->status = 0; /* clear status bits */

	/* Check for HS mode */
	if (i2cspeed == I2C_HS_MODE) {
		i2c->ctrl |= MXC_F_I2C_CTRL_HS_MODE; /* Enable HS mode */
	}

	/* Disable and clear interrupts */
	i2c->int_en0 = 0;
	i2c->int_en1 = 0;
	i2c->int_fl0 = i2c->int_fl0;
	i2c->int_fl1 = i2c->int_fl1;

	i2c->timeout = 0x0; /* set timeout */
	i2c->rx_ctrl0 |= MXC_F_I2C_RX_CTRL0_RX_FLUSH; /* clear the RX FIFO */
	i2c->tx_ctrl0 |= MXC_F_I2C_TX_CTRL0_TX_FLUSH; /* clear the TX FIFO */

	return I2C_Api_Setspeed(i2c, i2cspeed);
}

int I2C_Api_Shutdown(mxc_i2c_regs_t *i2c)
{
	int i2c_num, err;

	/* Check the base pointer  */
	i2c_num = MXC_I2C_GET_IDX(i2c);

	/* Disable and clear interrupts */
	i2c->int_en0 = 0;
	i2c->int_en1 = 0;
	i2c->int_fl0 = i2c->int_fl0;
	i2c->int_fl1 = i2c->int_fl1;

	i2c->rx_ctrl0 |= MXC_F_I2C_RX_CTRL0_RX_FLUSH; /* clear the RX FIFO */
	i2c->tx_ctrl0 |= MXC_F_I2C_TX_CTRL0_TX_FLUSH; /* clear the TX FIFO */

	/* Call all of the pending callbacks for this I2C */
	if (states[i2c_num].req != NULL) {
		I2C_Api_ClearInterrupts(i2c);
		I2C_Api_FreeCallback(i2c_num, EC_ERROR_UNKNOWN);
	}

	i2c->ctrl = 0;

	/* Clears system level configurations */
	if ((err = SYS_I2C_Shutdown(i2c)) != EC_SUCCESS) {
		return err;
	}

	return EC_SUCCESS;
}

int I2C_Api_MasterWrite(mxc_i2c_regs_t *i2c, uint8_t addr, int start, int stop,
			const uint8_t *data, int len, int restart)
{
	if (len == 0) {
		return EC_SUCCESS;
	}

	/* Clear the interrupt flag */
	i2c->int_fl0 = i2c->int_fl0;

	/* Make sure the I2C has been initialized */
	if (!(i2c->ctrl & MXC_F_I2C_CTRL_I2C_EN)) {
		return EC_ERROR_UNKNOWN;
	}

	/* Enable master mode */
	i2c->ctrl |= MXC_F_I2C_CTRL_MST;

	/* Load FIFO with slave address for WRITE and as much data as we can */
	while (i2c->status & MXC_F_I2C_STATUS_TX_FULL) {
	}

	if (start) {
		/** 
		 * The slave address is right-aligned, bits 6 to 0, shift
		 * to the left and make room for the write bit.
		 */
		i2c->fifo = (addr << 1) & ~(0x1);
	}

	while ((len > 0) && !(i2c->status & MXC_F_I2C_STATUS_TX_FULL)) {
		i2c->fifo = *data++;
		len--;
	}
	/* Generate Start signal */
	if (start) {
		i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_START;
	}

	/* Write remaining data to FIFO */
	while (len > 0) {
		/* Check for errors */
		if (i2c->int_fl0 & I2C_ERROR) {
			/* Set the stop bit */
			i2c->master_ctrl &= ~(MXC_F_I2C_MASTER_CTRL_RESTART);
			i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
			return EC_ERROR_UNKNOWN;
		}

		if (!(i2c->status & MXC_F_I2C_STATUS_TX_FULL)) {
			i2c->fifo = *data++;
			len--;
		}
	}
	/* Check if Repeated Start requested */
	if (restart) {
		i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_RESTART;
	} else {
		if (stop) {
			i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
		}
	}

	if (stop) {
		/* Wait for Done */
		while (!(i2c->int_fl0 & MXC_F_I2C_INT_FL0_DONE)) {
			/* Check for errors */
			if (i2c->int_fl0 & I2C_ERROR) {
				/* Set the stop bit */
				i2c->master_ctrl &=
					~(MXC_F_I2C_MASTER_CTRL_RESTART);
				i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
				return EC_ERROR_UNKNOWN;
			}
		}
		/* Clear Done interrupt flag */
		i2c->int_fl0 = MXC_F_I2C_INT_FL0_DONE;
	}

	/* Wait for Stop if requested and there is no restart. */
	if (stop && !restart) {
		while (!(i2c->int_fl0 & MXC_F_I2C_INT_FL0_STOP)) {
			/* Check for errors */
			if (i2c->int_fl0 & I2C_ERROR) {
				/* Set the stop bit */
				i2c->master_ctrl &= ~(MXC_F_I2C_MASTER_CTRL_RESTART);
				i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
				return EC_ERROR_UNKNOWN;
			}
		}
		/* Clear stop interrupt flag */
		i2c->int_fl0 = MXC_F_I2C_INT_FL0_STOP;
	}

	/* Check for errors */
	if (i2c->int_fl0 & I2C_ERROR) {
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

int I2C_Api_MasterRead(mxc_i2c_regs_t *i2c, uint8_t addr, int start, int stop,
		       uint8_t *data, int len, int restart)
{
	volatile int length = len;
	int interactive_receive_mode;

	if (len == 0) {
		return EC_SUCCESS;
	}

	if (len > 256) {
		return EC_ERROR_INVAL;
	}

	/* Clear the interrupt flag */
	i2c->int_fl0 = i2c->int_fl0;

	/* Make sure the I2C has been initialized */
	if (!(i2c->ctrl & MXC_F_I2C_CTRL_I2C_EN)) {
		return EC_ERROR_UNKNOWN;
	}

	/* Enable master mode */
	i2c->ctrl |= MXC_F_I2C_CTRL_MST;

	if (stop) {
		/* Set receive count */
		i2c->ctrl &= ~MXC_F_I2C_CTRL_RX_MODE;
		i2c->rx_ctrl1 = len;
		interactive_receive_mode = 0;
	} else {
		i2c->ctrl |= MXC_F_I2C_CTRL_RX_MODE;
		i2c->rx_ctrl1 = 1;
		interactive_receive_mode = 1;
	}

	/* Load FIFO with slave address */
	if (start) {
		i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_START;
		while (i2c->status & MXC_F_I2C_STATUS_TX_FULL) {
		}
		/** 
		 * The slave address is right-aligned, bits 6 to 0, shift
		 * to the left and make room for the read bit.
		 */
		i2c->fifo = ((addr << 1) | 1); 
	}

	/* Wait for all data to be received or error. */
	while (length > 0) {
		/* Check for errors */
		if (i2c->int_fl0 & I2C_ERROR) {
			/* Set the stop bit */
			i2c->master_ctrl &= ~(MXC_F_I2C_MASTER_CTRL_RESTART);
			i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
			return EC_ERROR_UNKNOWN;
		}

		/* if in interactive receive mode then ack each received byte */
		if (interactive_receive_mode) {
			while (!(i2c->int_fl0 & MXC_F_I2C_INT_EN0_RX_MODE))
				;
			if (i2c->int_fl0 & MXC_F_I2C_INT_EN0_RX_MODE) {
				/* read the data */
				*data++ = i2c->fifo;
				length--;
				/* clear the bit */
				if (length != 1) {
					i2c->int_fl0 =
						MXC_F_I2C_INT_EN0_RX_MODE;
				}
			}
		} else {
			if (!(i2c->status & MXC_F_I2C_STATUS_RX_EMPTY)) {
				*data++ = i2c->fifo;
				length--;
			}
		}
	}

	if (restart) {
		i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_RESTART;
	} else {
		if (stop) {
			i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
		}
	}

	/* Wait for Done */
	if (stop) {
		while (!(i2c->int_fl0 & MXC_F_I2C_INT_FL0_DONE)) {
			/* Check for errors */
			if (i2c->int_fl0 & I2C_ERROR) {
				/* Set the stop bit */
				i2c->master_ctrl &=
					~(MXC_F_I2C_MASTER_CTRL_RESTART);
				i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
				return EC_ERROR_UNKNOWN;
			}
		}
		/* Clear Done interrupt flag */
		i2c->int_fl0 = MXC_F_I2C_INT_FL0_DONE;
	}

	/* Wait for Stop */
	if (!restart) {
		if (stop) {
			while (!(i2c->int_fl0 & MXC_F_I2C_INT_FL0_STOP)) {
				/* Check for errors */
				if (i2c->int_fl0 & I2C_ERROR) {
					/* Set the stop bit */
					i2c->master_ctrl &= ~(
						MXC_F_I2C_MASTER_CTRL_RESTART);
					i2c->master_ctrl |=
						MXC_F_I2C_MASTER_CTRL_STOP;
					return EC_ERROR_UNKNOWN;
				}
			}
			/* Clear Stop interrupt flag */
			i2c->int_fl0 = MXC_F_I2C_INT_FL0_STOP;
		}
	}

	/* Check for errors */
	if (i2c->int_fl0 & I2C_ERROR) {
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

int I2C_Api_SlaveAsync(mxc_i2c_regs_t *i2c, i2c_req_t *req)
{
	int i2c_num;
	i2c_num = MXC_I2C_GET_IDX(i2c);

	/* Make sure the I2C has been initialized */
	if (!(i2c->ctrl & MXC_F_I2C_CTRL_I2C_EN)) {
		return EC_ERROR_UNKNOWN;
	}

	states[i2c_num].req = req;

	/* Disable master mode */
	i2c->ctrl &= ~(MXC_F_I2C_CTRL_MST);
	/* Set the Slave Address in the I2C peripheral register. */
	i2c->slave_addr = req->addr;

	/* Clear the byte counters */
	req->tx_num = 0;
	req->rx_num = 0;

	/* Disable and clear the interrupts */
	i2c->int_en0 = 0;
	i2c->int_en1 = 0;
	i2c->int_fl0 = i2c->int_fl0;
	i2c->int_fl1 = i2c->int_fl1;
	i2c->int_en0 = MXC_F_I2C_INT_EN0_ADDR_MATCH;

	return EC_SUCCESS;
}

/**
 * I2C_Api_SlaveRead() - Handles async read request from the I2c master.
 * @i2c: I2C Peripheral pointer for the 
 * @req: Pointer to the request info.
 * @int_flags: Current state of the interrupt flags for this request.
 */
static int I2C_Api_SlaveRead(mxc_i2c_regs_t *i2c, i2c_req_t *req, uint32_t int_flags) {
	int i2c_num;
#ifdef I2C_SEND_BYTE_EC
	int i;
#endif	
	i2c_num = MXC_I2C_GET_IDX(i2c);
	req->direction = I2C_TRANSFER_DIRECTION_MASTER_READ;
	if (slave_tx_remain != 0) {
		/* Fill the FIFO */
		while ((slave_tx_remain > 0) &&
				!(i2c->status &
				MXC_F_I2C_STATUS_TX_FULL)) {
			i2c->fifo = *(req->tx_data)++;
			states[i2c_num].num_wr++;
			slave_tx_remain--;
		}
		/* Set the TX threshold interrupt level */
		if (slave_tx_remain >=
			(MXC_I2C_FIFO_DEPTH - 1)) {
			i2c->tx_ctrl0 =
				((i2c->tx_ctrl0 &
					~(MXC_F_I2C_TX_CTRL0_TX_THRESH)) |
					(MXC_I2C_FIFO_DEPTH -
					1) << MXC_F_I2C_TX_CTRL0_TX_THRESH_POS);

		} else {
			i2c->tx_ctrl0 =
				((i2c->tx_ctrl0 &
				~(MXC_F_I2C_TX_CTRL0_TX_THRESH)) |
				(slave_tx_remain)
				<< MXC_F_I2C_TX_CTRL0_TX_THRESH_POS);
		}
		/* Enable TXTH interrupt and Error interrupts */
		i2c->int_en0 |= (MXC_F_I2C_INT_EN0_TX_THRESH |
					I2C_ERROR);
		if (int_flags & I2C_ERROR) {
			i2c->int_en0 = 0;
			/* Calculate the number of bytes sent by the slave */
			req->tx_num =
				states[i2c_num].num_wr -
				((i2c->tx_ctrl1 &
					MXC_F_I2C_TX_CTRL1_TX_FIFO) >>
					MXC_F_I2C_TX_CTRL1_TX_FIFO_POS);
			if (!req->sw_autoflush_disable) {
				/* Manually clear the TXFIFO */
				i2c->tx_ctrl0 |=
					MXC_F_I2C_TX_CTRL0_TX_FLUSH;
			}
			states[i2c_num].num_wr = 0;
			if (req->callback != NULL) {
				I2C_Api_ClearInterrupts(i2c);
				I2C_Api_FreeCallback(
					i2c_num,
					I2C_SLAVE_ERROR);
			}
			return EC_ERROR_UNKNOWN;
		}
	} else {
#ifdef I2C_SEND_BYTE_EC
		for (i = 0; i < 1; i++) {
			if (!(i2c->status &
					MXC_F_I2C_STATUS_TX_FULL)) {
				i2c->fifo = 0xec;
			}
		}
		/* set tx threshold to zero */
		i2c->tx_ctrl0 =
			((i2c->tx_ctrl0 &
				~(MXC_F_I2C_TX_CTRL0_TX_THRESH)) |
				(0) << MXC_F_I2C_TX_CTRL0_TX_THRESH_POS);
		/* Enable TXTH interrupt and Error interrupts */
		i2c->int_en0 |= (MXC_F_I2C_INT_EN0_TX_THRESH |
					I2C_ERROR);

#endif
	}
	return EC_SUCCESS;
}

/**
 * I2C_Api_SlaveWrite() - Handles async write request from the I2c master.
 * @i2c: I2C Peripheral pointer for the 
 * @req: Pointer to the request info.
 * @int_flags: Current state of the interrupt flags for this request.
 */
static int I2C_Api_SlaveWrite(mxc_i2c_regs_t *i2c, i2c_req_t *req, uint32_t int_flags) {
	int i2c_num;

	/**
	 * Master Write has been called and if there is a
	 * rx_data buffer
	 */
	i2c_num = MXC_I2C_GET_IDX(i2c);
	req->direction = I2C_TRANSFER_DIRECTION_MASTER_WRITE;
	if (slave_rx_remain != 0) {
		/* Read out any data in the RX FIFO */
		while ((slave_rx_remain > 0) &&
				!(i2c->status &
				MXC_F_I2C_STATUS_RX_EMPTY)) {
			*(req->rx_data)++ = i2c->fifo;
			req->rx_num++;
			slave_rx_remain--;
		}
		/* Set the RX threshold interrupt level */
		if (slave_rx_remain >=
			(MXC_I2C_FIFO_DEPTH - 1)) {
			i2c->rx_ctrl0 =
				((i2c->rx_ctrl0 &
				~(MXC_F_I2C_RX_CTRL0_RX_THRESH)) |
				(MXC_I2C_FIFO_DEPTH -
				1) << MXC_F_I2C_RX_CTRL0_RX_THRESH_POS);
		} else {
			i2c->rx_ctrl0 =
				((i2c->rx_ctrl0 &
				~(MXC_F_I2C_RX_CTRL0_RX_THRESH)) |
				(slave_rx_remain)
				<< MXC_F_I2C_RX_CTRL0_RX_THRESH_POS);
		}
		/* Enable RXTH interrupt and Error interrupts */
		i2c->int_en0 |= (MXC_F_I2C_INT_EN0_RX_THRESH |
					I2C_ERROR);
		if (int_flags & I2C_ERROR) {
			i2c->int_en0 = 0;
			/**
			 * Calculate the number of bytes sent 
			 * by the slave 
			 */
			req->tx_num =
				states[i2c_num].num_wr -
				((i2c->tx_ctrl1 &
				MXC_F_I2C_TX_CTRL1_TX_FIFO) >>
				MXC_F_I2C_TX_CTRL1_TX_FIFO_POS);

			if (!req->sw_autoflush_disable) {
				/* Manually clear the TXFIFO */
				i2c->tx_ctrl0 |=
				MXC_F_I2C_TX_CTRL0_TX_FLUSH;
			}
			states[i2c_num].num_wr = 0;
			if (req->callback != NULL) {
				I2C_Api_ClearInterrupts(i2c);
				I2C_Api_FreeCallback(
					i2c_num,
					I2C_SLAVE_ERROR);
			}
			return EC_ERROR_UNKNOWN;
		}
	} else {
		/* Disable RXTH interrupt */
		i2c->int_en0 &= ~(MXC_F_I2C_INT_EN0_RX_THRESH);
		/* Flush any extra bytes in the RXFIFO */
		i2c->rx_ctrl0 |= MXC_F_I2C_RX_CTRL0_RX_FLUSH;
		/* Store the current state of the slave */
		states[i2c_num].slave_state =
			I2C_SLAVE_READ_COMPLETE;
	}
	return EC_SUCCESS;
}



static void I2C_Api_SlaveHandler(mxc_i2c_regs_t *i2c)
{
	uint32_t int_flags;
	int i2c_num;
	i2c_req_t *req;
	int status;

	i2c_num = MXC_I2C_GET_IDX(i2c);
	req = states[i2c_num].req;

	/* Check for an Address match */
	if (i2c->int_fl0 & MXC_F_I2C_INT_FL0_ADDR_MATCH) {
		/* Clear AMI and TXLOI */
		i2c->int_fl0 |= MXC_F_I2C_INT_FL0_DONE;
		i2c->int_fl0 |= MXC_F_I2C_INT_FL0_ADDR_MATCH;
		i2c->int_fl0 |= MXC_F_I2C_INT_FL0_TX_LOCK_OUT;
		/* Store the current state of the Slave */
		states[i2c_num].slave_state = I2C_SLAVE_ADDR_MATCH;
		/* Set the Done, Stop interrupt */
		i2c->int_en0 |= MXC_F_I2C_INT_EN0_DONE | MXC_F_I2C_INT_EN0_STOP;
		/* Inhibit sleep mode when addressed until STOPF flag is set */
		disable_sleep(SLEEP_MASK_I2C_SLAVE);
	}

	/* Check for errors */
	int_flags = i2c->int_fl0;
	/* Clear the interrupts */
	i2c->int_fl0 = int_flags;

	if (int_flags & I2C_ERROR) {
		i2c->int_en0 = 0;
		/* Calculate the number of bytes sent by the slave */
		req->tx_num = states[i2c_num].num_wr -
			      ((i2c->tx_ctrl1 & MXC_F_I2C_TX_CTRL1_TX_FIFO) >>
			       MXC_F_I2C_TX_CTRL1_TX_FIFO_POS);

		if (!req->sw_autoflush_disable) {
			/* Manually clear the TXFIFO */
			i2c->tx_ctrl0 |= MXC_F_I2C_TX_CTRL0_TX_FLUSH;
		}
		states[i2c_num].num_wr = 0;
		if (req->callback != NULL) {
			I2C_Api_ClearInterrupts(i2c);
			I2C_Api_FreeCallback(i2c_num, I2C_SLAVE_ERROR);
		}
		return;
	}

	slave_rx_remain = req->rx_len - req->rx_num;
	slave_tx_remain = req->tx_len - states[i2c_num].num_wr;

	/* Check for Stop interrupt */
	if (int_flags & MXC_F_I2C_INT_FL0_STOP) {
		if (req->direction == I2C_TRANSFER_DIRECTION_MASTER_WRITE) {
			/* Read out any data in the RX FIFO */
			while (!(i2c->status & MXC_F_I2C_STATUS_RX_EMPTY)) {
				*(req->rx_data)++ = i2c->fifo;
				req->rx_num++;
			}
		}

		/* Disable all interrupts */
		i2c->int_en0 = 0;
		/* Calculate the number of bytes sent by the slave */
		req->tx_num = states[i2c_num].num_wr -
			      ((i2c->tx_ctrl1 & MXC_F_I2C_TX_CTRL1_TX_FIFO) >>
			       MXC_F_I2C_TX_CTRL1_TX_FIFO_POS);
		slave_rx_remain = 0;
		slave_tx_remain = 0;
		if (!req->sw_autoflush_disable) {
			/* Manually clear the TXFIFO */
			i2c->tx_ctrl0 |= MXC_F_I2C_TX_CTRL0_TX_FLUSH;
		}

		if (req->callback != NULL) {
			I2C_Api_ClearInterrupts(i2c);
			I2C_Api_FreeCallback(i2c_num, EC_SUCCESS);
		} else {
			i2c->int_fl0 = i2c->int_fl0;
			i2c->int_fl1 = i2c->int_fl1;
		}
		req->direction = I2C_TRANSFER_DIRECTION_NONE;
		states[i2c_num].num_wr = 0;

		/* Be ready to receive more data */
		req->rx_len = 128;
		/* Clear the byte counters */
		req->tx_num = 0;
		req->rx_num = 0;
		/* Disable and clear the interrupts */
		i2c->int_en0 = 0;
		i2c->int_en1 = 0;
		i2c->int_fl0 = i2c->int_fl0;
		i2c->int_fl1 = i2c->int_fl1;
		i2c->int_en0 = MXC_F_I2C_INT_EN0_ADDR_MATCH;

		/* No longer inhibit deep sleep after stop condition */
		enable_sleep(SLEEP_MASK_I2C_SLAVE);
		return;
	}

	/* Check for DONE interrupt */
	if (int_flags & MXC_F_I2C_INT_FL0_DONE) {
		if (req->direction == I2C_TRANSFER_DIRECTION_MASTER_WRITE) {
			/* Read out any data in the RX FIFO */
			while (!(i2c->status & MXC_F_I2C_STATUS_RX_EMPTY)) {
				*(req->rx_data)++ = i2c->fifo;
				req->rx_num++;
			}
		}
		/* Disable Done interrupt */
		i2c->int_en0 &= ~(MXC_F_I2C_INT_EN0_DONE);
		/* Calculate the number of bytes sent by the slave */
		req->tx_num = states[i2c_num].num_wr -
			      ((i2c->tx_ctrl1 & MXC_F_I2C_TX_CTRL1_TX_FIFO) >>
			       MXC_F_I2C_TX_CTRL1_TX_FIFO_POS);
		slave_rx_remain = 0;
		slave_tx_remain = 0;
		if (!req->sw_autoflush_disable) {
			/* Manually clear the TXFIFO */
			i2c->tx_ctrl0 |= MXC_F_I2C_TX_CTRL0_TX_FLUSH;
		}
		I2C_Api_FreeCallback(i2c_num, EC_SUCCESS);
		req->direction = I2C_TRANSFER_DIRECTION_NONE;
		states[i2c_num].num_wr = 0;
		return;
	}

	if (states[i2c_num].slave_state != I2C_SLAVE_ADDR_MATCH) {
		return;
	}
	/**
	 * Check if Master Read has been called and if there is a
	 * tx_data buffer
	 */
	if (i2c->ctrl & MXC_F_I2C_CTRL_READ) {
		status = I2C_Api_SlaveRead(i2c, req, int_flags);
		if (status != EC_SUCCESS) {
			return;
		}
	} else {
		status = I2C_Api_SlaveWrite(i2c, req, int_flags);
		if (status != EC_SUCCESS) {
			return;
		}
	}
}

void I2C_Api_Handler(mxc_i2c_regs_t *i2c)
{
	I2C_Api_SlaveHandler(i2c);
}

static void I2C_Api_FreeCallback(int i2c_num, int error)
{
	/* Save the request */
	i2c_req_t *temp_req = states[i2c_num].req;

	/* Callback if not NULL */
	if (temp_req->callback != NULL) {
		temp_req->callback(temp_req, error);
	}
}

static void I2C_Api_ClearInterrupts(mxc_i2c_regs_t *i2c)
{
	/* Disable and clear interrupts */
	i2c->int_en0 = 0;
	i2c->int_en1 = 0;
	i2c->int_fl0 = i2c->int_fl0;
	i2c->int_fl1 = i2c->int_fl1;
	i2c->ctrl = 0;
	i2c->ctrl = MXC_F_I2C_CTRL_I2C_EN;
}

int I2C_Api_AbortAsync(i2c_req_t *req)
{
	int i2c_num;
	mxc_i2c_regs_t *i2c;

	/* Find the request, set to NULL */
	for (i2c_num = 0; i2c_num < MXC_I2C_INSTANCES; i2c_num++) {
		if (req == states[i2c_num].req) {
			i2c = MXC_I2C_GET_I2C(i2c_num);
			I2C_Api_ClearInterrupts(i2c);
			I2C_Api_FreeCallback(i2c_num, EC_ERROR_UNKNOWN);

			return EC_SUCCESS;
		}
	}

	return EC_ERROR_INVAL;
}
