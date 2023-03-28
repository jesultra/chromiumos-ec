/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpc_bep_bio_algorithm.h"
#include "fpc_bep_private.h"
#include "fpsensor.h"
#include "fpsensor_driver.h"
#include "fpsensor_utils.h"
#include "gpio.h"
#include "spi.h"
#include "system.h"
#include "util.h"

#include <stddef.h>
#include <stdint.h>

/* Recorded error flags */
static uint16_t errors;

/* FPC specific initialization and de-initialization functions */
int fp_sensor_open(void);
int fp_sensor_close(void);

/* Get FPC library version code.*/
const char *fp_sensor_get_version(void);

/* Get FPC library build info.*/
const char *fp_sensor_get_build_info(void);

/* Sensor description */
static struct ec_response_fp_info ec_fp_sensor_info = {
	/* Sensor identification */
	.vendor_id = FOURCC('F', 'P', 'C', ' '),
	.product_id = 9,
	.model_id = 1,
	.version = 1,
	/* Image frame characteristics */
	.frame_size = FP_SENSOR_IMAGE_SIZE_FPC,
	.pixel_format = V4L2_PIX_FMT_GREY,
	.width = FP_SENSOR_RES_X_FPC,
	.height = FP_SENSOR_RES_Y_FPC,
	.bpp = FP_SENSOR_RES_BPP_FPC,
};

typedef struct fpc_bep_sensor fpc_bep_sensor_t;

typedef struct {
	const fpc_bep_sensor_t *sensor;
	uint32_t image_buffer_size;
} fpc_sensor_info_t;

#if defined(HAVE_PRIVATE)
static uint8_t
	enroll_ctx[FP_ALGORITHM_ENROLLMENT_SIZE_FPC] __aligned(4) = { 0 };

#if defined(CONFIG_FP_SENSOR_FPC1025)

extern const fpc_bep_sensor_t fpc_bep_sensor_1025;
extern const fpc_bep_algorithm_t fpc_bep_algorithm_pfe_1025;

const fpc_sensor_info_t fpc_sensor_info = {
	.sensor = &fpc_bep_sensor_1025,
	.image_buffer_size = FP_SENSOR_IMAGE_SIZE_FPC,
};

const fpc_bio_info_t fpc_bio_info = {
	.algorithm = &fpc_bep_algorithm_pfe_1025,
	.template_size = FP_ALGORITHM_TEMPLATE_SIZE_FPC,
};

#elif defined(CONFIG_FP_SENSOR_FPC1035)

extern const fpc_bep_sensor_t fpc_bep_sensor_1035;
extern const fpc_bep_algorithm_t fpc_bep_algorithm_pfe_1035;

const fpc_sensor_info_t fpc_sensor_info = {
	.sensor = &fpc_bep_sensor_1035,
	.image_buffer_size = FP_SENSOR_IMAGE_SIZE,
};

const fpc_bio_info_t fpc_bio_info = {
	.algorithm = &fpc_bep_algorithm_pfe_1035,
	.template_size = FP_ALGORITHM_TEMPLATE_SIZE,
};
#else
#error "Sensor type not defined!"
#endif

#else /* defined(HAVE_PRIVATE) */

/*
 * Private is not defined, so create stubs for required functions from private
 * libraries
 */
void fp_sensor_configure_detect(void)
{
}

enum finger_state fp_sensor_finger_status(void)
{
	return FINGER_NONE;
}

int fp_sensor_acquire_image_with_mode(uint8_t *image_data, int mode)
{
	return EC_ERROR_INVAL;
}

#endif /* defined(HAVE_PRIVATE) */

/* Sensor IC commands */
enum fpc_cmd {
	FPC_CMD_DEEPSLEEP = 0x2C,
	FPC_CMD_HW_ID = 0xFC,
};

/* Maximum size of a sensor command SPI transfer */
#define MAX_CMD_SPI_TRANSFER_SIZE 3

/* Memory for the SPI transfer buffer */
static uint8_t spi_buf[MAX_CMD_SPI_TRANSFER_SIZE];

static int fpc_send_cmd(const uint8_t cmd)
{
	spi_buf[0] = cmd;

	return spi_transaction(SPI_FP_DEVICE, spi_buf, 1, spi_buf,
			       SPI_READBACK_ALL);
}

static void fp_sensor_low_power(void)
{
	fpc_send_cmd(FPC_CMD_DEEPSLEEP);
}

int fpc_get_hwid(uint16_t *id)
{
	int rc;
	uint16_t sensor_id;

	if (id == NULL)
		return EC_ERROR_INVAL;

	spi_buf[0] = FPC_CMD_HW_ID;

	rc = spi_transaction(SPI_FP_DEVICE, spi_buf, 3, spi_buf,
			     SPI_READBACK_ALL);
	if (rc) {
		CPRINTS("FPC HW ID read failed %d", rc);
		return FP_ERROR_SPI_COMM;
	}

	sensor_id = ((spi_buf[1] << 8) | spi_buf[2]);
	*id = sensor_id;

	return EC_SUCCESS;
}

static int fpc_check_hwid(void)
{
	uint16_t id = 0;
	int status;

	status = fpc_get_hwid(&id);
	if ((id >> 4) != FP_SENSOR_HWID_FPC) {
		CPRINTS("FPC unknown silicon 0x%04x", id);
		return FP_ERROR_BAD_HWID;
	}
	if (status == EC_SUCCESS)
		CPRINTS(FP_SENSOR_NAME_FPC " id 0x%04x", id);

	return status;
}

/* Reset and initialize the sensor IC */
static int fp_sensor_init(void)
{
#if !defined(HAVE_PRIVATE)
	return EC_ERROR_INVAL;
#else
	int rc;

	/* Print the binary libfpbep.a library version */
	CPRINTS("FPC libfpbep.a %s", fp_sensor_get_version());

	/* Print the BEP version and build time of the library */
	CPRINTS("Build information - %s", fp_sensor_get_build_info());

	errors = FP_ERROR_DEAD_PIXELS_UNKNOWN;

	rc = fp_sensor_open();
	if (rc) {
		errors |= FP_ERROR_INIT_FAIL;
		CPRINTS("Error: fp_sensor_open() failed, result=%d", rc);
	}

	errors |= fpc_check_hwid();

	rc = bio_algorithm_init();
	if (rc < 0) {
		errors |= FP_ERROR_INIT_FAIL;
		CPRINTS("Error: bio_algorithm_init() failed, result=%d", rc);
	}

	/* Go back to low power */
	fp_sensor_low_power();

	return EC_SUCCESS;
#endif
}

/* Deinitialize the sensor IC */
static int fp_sensor_deinit(void)
{
#if !defined(HAVE_PRIVATE)
	return EC_ERROR_INVAL;
#else
	int rc;

	rc = bio_algorithm_exit();
	if (rc < 0)
		CPRINTS("Error: bio_algorithm_exit() failed, result=%d", rc);

	rc = fp_sensor_close();
	if (rc < 0)
		CPRINTS("Error: fp_sensor_close() failed, result=%d", rc);

	return rc;
#endif
}

static int fp_sensor_get_info(struct ec_response_fp_info *resp)
{
	int rc;

	spi_buf[0] = FPC_CMD_HW_ID;

	memcpy(resp, &ec_fp_sensor_info, sizeof(struct ec_response_fp_info));

	rc = spi_transaction(SPI_FP_DEVICE, spi_buf, 3, spi_buf,
			     SPI_READBACK_ALL);
	if (rc)
		return EC_RES_ERROR;

	resp->model_id = (spi_buf[1] << 8) | spi_buf[2];
	resp->errors = errors;

	return EC_SUCCESS;
}

int fp_finger_match(void *templ, uint32_t templ_count, uint8_t *image,
		    int32_t *match_index, uint32_t *update_bitmap)
{
#if !defined(HAVE_PRIVATE)
	return EC_ERROR_INVAL;
#else
	int rc;

	rc = bio_template_image_match_list(templ, templ_count, image,
					   match_index, update_bitmap);
	if (rc < 0)
		CPRINTS("Error: bio_template_image_match_list() failed, result=%d",
			rc);

	return rc;
#endif
}

static int fp_enrollment_begin(void)
{
#if !defined(HAVE_PRIVATE)
	return EC_ERROR_INVAL;
#else
	int rc;
	bio_enrollment_t bio_enroll = enroll_ctx;

	rc = bio_enrollment_begin(&bio_enroll);
	if (rc < 0)
		CPRINTS("Error: bio_enrollment_begin() failed, result=%d", rc);

	return rc;
#endif
}

static int fp_enrollment_finish(void *templ)
{
#if !defined(HAVE_PRIVATE)
	return EC_ERROR_INVAL;
#else
	int rc;
	bio_enrollment_t bio_enroll = enroll_ctx;
	bio_template_t bio_templ = templ;

	rc = bio_enrollment_finish(bio_enroll, templ ? &bio_templ : NULL);
	if (rc < 0)
		CPRINTS("Error: bio_enrollment_finish() failed, result=%d", rc);

	return rc;
#endif
}

int fp_finger_enroll(uint8_t *image, int *completion)
{
#if !defined(HAVE_PRIVATE)
	return EC_ERROR_INVAL;
#else
	int rc;
	bio_enrollment_t bio_enroll = enroll_ctx;

	rc = bio_enrollment_add_image(bio_enroll, image);
	if (rc < 0) {
		CPRINTS("Error: bio_enrollment_add_image() failed, result=%d",
			rc);
		return rc;
	}

	*completion = bio_enrollment_get_percent_complete(bio_enroll);

	return rc;
#endif
}

static int fp_maintenance(void)
{
	return fpc_sensor_maintenance(&errors);
}

struct fp_sensor_interface fp_driver_bep = {
	.sensor_type = FP_SENSOR_TYPE_FPC,
	.sensor_hwid = FP_SENSOR_HWID_FPC,
	.sensor_init = &fp_sensor_init,
	.sensor_deinit = &fp_sensor_deinit,
	.sensor_get_info = &fp_sensor_get_info,
	.sensor_low_power = &fp_sensor_low_power,
	.sensor_configure_detect = &fp_sensor_configure_detect,
	.sensor_finger_status = &fp_sensor_finger_status,
	.sensor_acquire_image_with_mode = &fp_sensor_acquire_image_with_mode,
	.finger_enroll = &fp_finger_enroll,
	.finger_match = &fp_finger_match,
	.enrollment_begin = &fp_enrollment_begin,
	.enrollment_finish = &fp_enrollment_finish,
	.maintenance = &fp_maintenance,
	.image_size = FP_SENSOR_IMAGE_SIZE_FPC,
	.template_size = FP_ALGORITHM_TEMPLATE_SIZE_FPC,
	.encrypted_template_size =
		FP_ALGORITHM_TEMPLATE_SIZE_FPC + FP_POSITIVE_MATCH_SALT_BYTES +
		sizeof(struct ec_fp_template_encryption_metadata),
	.res_x = FP_SENSOR_RES_X_FPC,
	.res_y = FP_SENSOR_RES_Y_FPC
};

struct fp_sensor_interface *fpc_sensor_get_interface(void)
{
	return &fp_driver_bep;
}
