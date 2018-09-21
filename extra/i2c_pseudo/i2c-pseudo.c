/*
 * This Linux kernel module implements pseudo I2C adapters that can be backed
 * by userspace programs.  This allows for implementing an I2C bus from
 * userspace, which can tunnel the I2C commands through another communication
 * channel to a remote I2C bus.
 */
/* TODO: Change remaining pr_err(), pr_debug(), etc to dev_info(&i2c_pseudo_device, ...), dev_dbg(&i2c_pseudo_device, ...), etc.  Except in i2c_pseudo_init and i2c_pseudo_exit, because i2c_pseudo_device might not be initialized. */

#include <linux/build_bug.h>
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

/* TODO: Delete this. */
#define I2C_PSEUDO_ENABLE_CTRLR_ERROR_CMD
#undef I2C_PSEUDO_ENABLE_CTRLR_ERROR_CMD

/* TODO: Delete this. */
#undef I2C_PSEUDO_CHECK_FOR_CDEV_RELEASE_RACE
#define I2C_PSEUDO_CHECK_FOR_CDEV_RELEASE_RACE

/* TODO: move these to include/linux/kernel.h */
#define SSIZE_T_MAX ((ssize_t)(~(size_t)0 >> 1))
#define SSIZE_T_MIN (-SSIZE_T_MAX - 1)

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

/* Expects an array or pointer. */
#define ARRAYLEN(x) (sizeof(x) / sizeof(*(x)))
/* Expects a "string" literal. */
#define STRLEN(x) (sizeof(x) - 1)

/* Minimum i2c_pseudo_limit module parameter value. */
#define I2C_PSEUDO_ADAPTERS_MIN 0
/* Maximum i2c_pseudo_limit module parameter value. */
#define I2C_PSEUDO_ADAPTERS_MAX (1<<8)
/* Value for alloc_chrdev_region() baseminor arg. */
#define I2C_PSEUDO_CDEV_BASEMINOR 0
/* Value for alloc_chrdev_region() count arg.  Should always be 1. */
#define I2C_PSEUDO_CDEV_COUNT 1

/* Used in struct i2c_adapter.name field. */
#define I2C_PSEUDO_ADAPTER_PREFIX "I2C pseudo adapter "
/* Used in struct device.kobj.name field. */
#define I2C_PSEUDO_DEVICE_NAME "i2c-pseudo-controller"
/* Value for struct cdev.kobj.name field. */
#define I2C_PSEUDO_CDEV_NAME "i2c-pseudo"
/* Value for alloc_chrdev_region() name arg. */
#define I2C_PSEUDO_CHRDEV_NAME "i2c_pseudo"
/* Value for class_create() name arg. */
#define I2C_PSEUDO_CLASS_NAME "i2c-pseudo"

#define I2C_PSEUDO_BEGIN_MXFER_REQ_CMD "I2C_BEGIN_XFER"
#define I2C_PSEUDO_COMMIT_MXFER_REQ_CMD "I2C_COMMIT_XFER"
#define I2C_PSEUDO_MXFER_REQ_CMD "I2C_XFER_REQ"
#define I2C_PSEUDO_MXFER_REPLY_CMD "I2C_XFER_REPLY"
#ifdef I2C_PSEUDO_ENABLE_CTRLR_ERROR_CMD
#define I2C_PSEUDO_CTRLR_ERROR_CMD "CTRLR_ERR"
#endif
#define I2C_PSEUDO_ADAP_SHUTDOWN_CMD "ADAPTER_SHUTDOWN"

/* TODO: Change terminology to stop talking about controller "commands" coming from the controller backend, and instead make kernel<->userspace_backend direction of flow clear, e.g. "controller write input," "controller read response." */
/* Maximum size of a controller command. */
#define I2C_PSEUDO_CTRLR_CMD_BUF_SIZE (1<<8)
/* Maximum number of controller read responses to allow enqueued at once. */
#define I2C_PSEUDO_CTRLR_RSP_QUEUE_LIMIT (1<<8)
/* The maximum size of a single controller read response. */
#define I2C_PSEUDO_MAX_MSG_BUF_SIZE (1<<14)
/* Maximum length (not size!) of i2c_pseudo_cmds static array. */
#define I2C_PSEUDO_CMDS_SANITY_LIMIT (1<<6)

/*
 * Marks the end of a controller command or read response.
 *
 * Fundamentally, controller commands and read responses could use different end
 * marker characters, but for sanity they should be the same.
 */
static const char i2c_pseudo_ctrlr_end_char = '\n';
/* Separator between I2C message header fields in the controller bytestream. */
static const char i2c_pseudo_ctrlr_header_sep_char = ' ';
/* Separator between I2C message data bytes in the controller bytestream. */
static const char i2c_pseudo_ctrlr_data_sep_char = ':';

/*
 * The number of pseudo I2C adapters permitted.  This default value can be
 * overridden at module load time.  Must be in the range
 * [I2C_PSEUDO_ADAPTERS_MIN, I2C_PSEUDO_ADAPTERS_MAX].
 *
 * As currently used, this MUST NOT be changed during or after module
 * initialization.  If the ability to change this at runtime is desired, an
 * audit of the uses of this variable will be necessary.
 */
static unsigned int i2c_pseudo_limit = 8;
module_param(i2c_pseudo_limit, uint, S_IRUGO);

/* TODO: Make the adapter timeout configurable by the controller.  Maintain this module parameter as the default for new adapters, renamed to i2c_pseudo_default_timeout_ms. */
/*
 * The I2C pseudo adapter timeout, in milliseconds.
 * 0 means use Linux I2C adapter default.
 */
static unsigned short i2c_pseudo_timeout_ms = 5 * 1000;
module_param(i2c_pseudo_timeout_ms, ushort, S_IRUGO);

/*
 * This should be an unsigned type large enough to hold I2C_PSEUDO_ADAPTERS_MAX.
 */
typedef unsigned int i2c_pseudo_ctrlr_id_t;
typedef unsigned int i2c_pseudo_mxfer_id_t;
struct i2c_pseudo_controller;

/*
 * Locking rules:
 *
 * - Never allow interruptions from non-killable signals.
 *
 * - Allow interruption from killable signals when acquiring to add a new
 * I2C pseudo controller.
 *
 * - Do _not_ allow interruption when acquiring to remove an
 * I2C pseudo controller that would otherwise become abandoned.
 */
struct i2c_pseudo_counters {
	/* This must be held while accessing any fields. */
	struct mutex lock;
	/* TODO: Export the count in sysfs, or get rid of it, since it's not actually needed or used for enforcing i2c_pseudo_limit. */
	unsigned int count;
	/*
	 * This is used to make a strong attempt at avoiding ID reuse,
	 * especially during the lifetime of a userspace i2c-dev client.  This
	 * can wrap by design, and thus makes no perfect guarantees.
	 */
	i2c_pseudo_ctrlr_id_t next_ctrlr_id;
	struct i2c_pseudo_controller **all_controllers;
};

static struct i2c_pseudo_counters i2c_pseudo_counters;
static dev_t i2c_pseudo_dev_num;
static struct class *i2c_pseudo_class;
static struct cdev i2c_pseudo_cdev;
static struct device i2c_pseudo_device;

struct i2c_pseudo_cmd {
	char *cmd_string;
	size_t cmd_size;

	/*
	 * TODO: Document the requirements of and guarantees for this callback.
	 *
	 * Include the following:
	 *
	 * - May be NULL.
	 */
	int (*data_creator)(void **data);
	/*
	 * TODO: Document the requirements of and guarantees for this callback.
	 *
	 * Include the following:
	 *
	 * - This is guaranteed to be called exactly once after each successful
	 *   call to data_creator, and will be passed the same data pointer
	 *   that function placed in its **data output arg.
	 *
	 * - The *data pointer will not be used again by the write command
	 *   system after the start of this function call.
	 *
	 * - May be NULL.
	 */
	void (*data_destroyer)(void *data);
	/*
	 * TODO: Document the requirements of and guarantees for this callback.
	 *
	 * Include the following:
	 *
	 * - Invoked once for each header field, in order, including the initial
	 *   command name e.g. for "I2C_XFER_REPLY" .
	 *
	 * - in[in_size] is guaranteed to be null.  There may be null characters
	 *   inside the formal buffer boundaries as well though!
	 *
	 * - Meaning of a negative return value: Negative error code.  The
	 *   receiver will never be called again for the same write command.
	 *
	 * - Meaning of a zero return value: The input was successfully
	 *   processed, and a header field is expected next, which should be
	 *   fully buffered before being sent to the receiver.
	 *
	 * - Meaning of a positive return value: The input was successfully processed, and data is expected next, which should be sent to data_receiver in increments of this many bytes.  The actual amount sent in each data_receiver call will always be a multiple of this number, except when the data block or write command is terminated after a number of data bytes which is not a multiple of this command.  Also, data_receiver will never be sent zero bytes, and will never be sent I2C_PSEUDO_CTRLR_CMD_BUF_SIZE or more bytes.  There are no other guarantees about how many bytes will be sent in each call to data_receiver.
	 *
	 * - May be NULL.
	 */
	int (*header_receiver)(void *data, char *in, size_t in_size,
		bool non_blocking);
	/*
	 * TODO: Document the requirements of and guarantees for this callback.
	 *
	 * Include the following:
	 *
	 * - Invoked for data fields, as requested by header_receiver return values.  May be invoked multiple times for each data field, with the data broken up into sequential non-overlapping chunks.
	 *
	 * - in[in_size] is guaranteed to be null.  There may be null characters
	 *   inside the formal buffer boundaries as well though!
	 *
	 * - in_size is guaranteed to be greater than zero, and less than I2C_PSEUDO_CTRLR_CMD_BUF_SIZE.
	 *
	 * - A positive return value has no meaning and must not be returned.
	 *
	 * - A zero return value indicates success.
	 *
	 * - A negative return value indicates an error and should be a negative
	 *   error code.  This error may or may not be returned to the I2C
	 *   client.
	 *
	 * - If header_receiver never returns a positive value, this function
	 *   will never be called, and it can be NULL.
	 *
	 * - If header_receiver is NULL or never returns a positive number, this SHOULD be NULL.  Conversely, if header_receiver can ever return a positive number, this MUST NOT be NULL.
	 */
	int (*data_receiver)(void *data, char *in, size_t in_size,
		bool non_blocking);
	/*
	 * TODO: Document the requirements of and guarantees for this callback.
	 *
	 * Include the following:
	 *
	 * - If @receive_status is positive, it is an error code from the
	 *   invoking routines themselves, e.g. if the controller process wrote
	 *   a header field too large for I2C_PSEUDO_CTRLR_CMD_BUF_SIZE.
	 *
	 * - If @receive_status is zero, it means all invocations of
	 *   header_receiver and data_receiver returned successful values and
	 *   the entire write command was received successfully.
	 *
	 * - If @receive_status is negative, it is the value returned by the
	 *   last header_receiver or data_receiver invocation.
	 *
	 * - This is called exactly once for each write command.  This is true
	 *   regardless of the value of @non_blocking and regardless of the
	 *   return value of this function, so it is imperative that this
	 *   function perform any necessary cleanup tasks related to @data, even
	 *   if non_blocking=true and blocking is required!
	 *
	 * - Thus, even with non_blocking=true, it would only ever make sense to
	 *   return -EAGAIN from this function if the struct i2c_pseudo_cmd
	 *   implementation is able to perform the would-be blocked
	 *   cmd_completer operation later, e.g. upon invocation of a callback
	 *   for the next write command, or by way of a background thread.
	 *
	 * - An error should be returned only to indicate a new error that
	 *   happened during the execution of the callback.  Any error from
	 *   @receive_status should *not* be copied to the return value of this
	 *   callback.
	 *
	 * - May be NULL.
	 */
	int (*cmd_completer)(void *data, int receive_status, bool non_blocking);
};

struct i2c_pseudo_cmd_mxfer_reply_data {
	/* This must be held while read or writing reply_queue_* fields. */
	struct mutex reply_queue_lock;
	/*
	 * This is used to make a strong attempt at avoiding ID reuse,
	 * especially for overlapping master_xfer() calls.
	 * This can wrap by design, and thus makes no perfect guarantees.
	 * No code should assume uniqueness, not even for master_xfer() calls of
	 * overlapping lifetimes.  When the controller writes a master_xfer()
	 * reply command, assume that it is for the oldest outstanding instance
	 * of the ID number specified.
	 */
	i2c_pseudo_mxfer_id_t next_mxfer_id;
	/*
	 * This is a FIFO queue of
	 * struct i2c_pseudo_cmd_mxfer_reply.reply_queue_item .
	 *
	 * This MUST be strictly used as FIFO.  Only consume or pop the first
	 * item.  Only append to the end.  Users of this queue assume this FIFO
	 * behavior is strictly followed, and their uses of reply_queue_lock may
	 * not be safe otherwise.
	 */
	struct list_head reply_queue_head;
	unsigned int reply_queue_length;
};

enum i2c_pseudo_cmd_mxfer_reply_state {
	/*
	 * The state for receiving the first field must have a zero value, so
	 * that zero-initialized memory for this field results in this as the
	 * default value.
	 */
	I2C_PSEUDO_CMD_MXFER_REPLY_STATE_CMD_NEXT = 0,
	I2C_PSEUDO_CMD_MXFER_REPLY_STATE_ADDR_NEXT,
	I2C_PSEUDO_CMD_MXFER_REPLY_STATE_FLAGS_NEXT,
	I2C_PSEUDO_CMD_MXFER_REPLY_STATE_ERRNO_NEXT,
	I2C_PSEUDO_CMD_MXFER_REPLY_STATE_DATA_NEXT,
	/*
	 * This is used to tell subsequent callback invocations that the write
	 * command currently being received is invalid, when the receiver wants
	 * to quietly discard the write command instead of loudly returning an
	 * error.
	 */
	I2C_PSEUDO_CMD_MXFER_REPLY_STATE_INVALID,
};

/* All values must be >= 0. */
enum i2c_pseudo_cmd_completer_retval {
	I2C_PSEUDO_CMD_COMPLETER_SUCCESS = 0,
	/* Tells the I2C pseudo adapter driver to shutdown the adapter. */
	I2C_PSEUDO_CMD_COMPLETER_SHUTDOWN,
};

struct i2c_pseudo_cmd_mxfer_reply {
	/*
	 * This lock MUST be held while reading or modifying any part of this
	 * struct i2c_pseudo_cmd_mxfer_reply, unless you can guarantee that
	 * nothing else can access this struct concurrently, such as during
	 * initialization.
	 *
	 * The struct i2c_pseudo_cmd_mxfer_reply_data.reply_queue_lock of the
	 * struct i2c_pseudo_cmd_mxfer_reply_data.reply_queue_head list which
	 * contains this struct i2c_pseudo_cmd_mxfer_reply.reply_queue_item MUST
	 * be held when attempting to acquire this lock.
	 *
	 * It is NOT required to keep
	 * struct i2c_pseudo_cmd_mxfer_reply_data.reply_queue_lock held after
	 * acquisition of this lock (unless also manipulating
	 * struct i2c_pseudo_cmd_mxfer_reply_data.reply_queue_* of course).
	 */
	struct mutex lock;

	/* TODO: Either put this to use in the controller protocol, or drop it, or at least find some introspection use for it if keeping it around. */
	i2c_pseudo_mxfer_id_t id;
	/* Number of I2C messages successfully processed, or negative error. */
	int ret;
	/* Same type as struct i2c_algorithm.master_xfer @num arg. */
	int num_msgs;
	/* Same type as struct i2c_algorithm.master_xfer @msgs arg. */
	struct i2c_msg *msgs;

	enum i2c_pseudo_cmd_mxfer_reply_state state;
	/*
	 * current_addr should always be filled in while
	 * state is greater than I2C_PSEUDO_CMD_MXFER_REPLY_STATE_ADDR_NEXT.
	 */
	/* Same type as struct i2c_msg.addr field. */
	u16 current_addr;
	/* Same type as struct i2c_algorithm.master_xfer @num arg. */
	int current_msg_idx;
	/* Same type as struct i2c_msg.len field. */
	u16 current_buf_idx;

	/*
	 * This is for use in
	 * struct i2c_pseudo_cmd_mxfer_reply_data.reply_queue_head FIFO queue.
	 *
	 * Any time this is deleted from its containing
	 * struct i2c_pseudo_cmd_mxfer_reply_data.reply_queue_head list, either
	 * list_del_init() MUST be used (not list_del()), OR this whole
	 * struct i2c_pseudo_cmd_mxfer_reply MUST be freed.
	 *
	 * That way, if this struct is not immediately freed, the code which
	 * eventually frees it can test whether it still needs to be deleted
	 * from struct i2c_pseudo_cmd_mxfer_reply_data.reply_queue_head by using
	 * list_empty() on reply_queue_item.  (Calling list_del() on an
	 * already-deleted list item is unsafe.)
	 */
	struct list_head reply_queue_item;
	struct completion data_filled;
};

struct i2c_pseudo_rsp {
	/*
	 * TODO: Document the requirements of and guarantees for this callback.
	 *
	 * Include the following:
	 * - There will never be more than one formatter callback in flight at once for a given pseudo I2C adapter.  This is true even in the face of concurrent reads by the controller.
	 * - Upon positive return value, *out must be set to a buffer which the caller will take ownership of, and which can be freed with kfree().
	 * - Upon positive return value, data must NOT be freed.
	 * - The formatter will be called repeatedly for the same data until it returns non-positive.
	 * - Upon non-positive return value, *out should not be modified.
	 * - Upon non-positive return value, the formatter should have freed data with kfree().  Implicitly this means any allocations owned by *data should have been freed by the formatter as well.
	 * - The formatter owns data.  The invoking code will never mutate or free data.  Thus, upon non-positive return value from the formatter, the formatter must have already performed any reference counting decrement or memory freeing necessary to ensure data does not live beyond its final use.
	 * - A negative return value indicates an error occured and the data cannot be formatted successfuly.  The error code may or may not eventually be propagated back to the I2C pseudo adapter controller.
	 * - A positive return value is the number of characters/bytes to use from the *out buffer, always starting from index 0.  It should NOT include a trailing NULL character unless that character should be propagated to the I2C pseudo adapter controller!  It therefore does NOT need to be the full size of the allocated *out buffer, instead it can be less.  (The size is not needed by kfree().)
	 * - The formatter must NOT use i2c_pseudo_ctrlr_end_char in anywhere in *out (within the size range indicated by the return value; past that does not matter).  The i2c_pseudo_ctrlr_end_char will be added automatically by the caller after a zero return value (successful completion) from the formatter.
	 * - The formatter must never create or return a buffer larger than I2C_PSEUDO_MAX_MSG_BUF_SIZE.  The formatter is encouraged to avoid that by generating and returning the output in chunks, taking advantage of the guarantee that it will be called repeatedly until exhaustion (zero return value) or failure (negative return value).  If the formatter expects its formatted output or natural subsets of it to always fit within I2C_PSEUDO_MAX_MSG_BUF_SIZE, and it is called with input data not meeting that expectation, the formatter should return -ERANGE to indicate this condition.
	 */
	ssize_t (*formatter)(void *data, char **out);
	void *data;

	struct list_head queue;
};

struct i2c_pseudo_rsp_buffer {
	char *buf;
	size_t size;
};

/* TODO: Delete struct i2c_pseudo_rsp_error and i2c_pseudo_rsp_error_formatter().  Instead place pre-formatted error messages in struct i2c_pseudo_rsp_buffer for use with i2c_pseudo_rsp_buffer_formatter(). */
struct i2c_pseudo_rsp_error {
	/*
	 * Do not use any of the following in an error message:
	 * - '\0' (null character)
	 * - i2c_pseudo_ctrlr_end_char
	 *
	 * Messages with either may be truncated or rejected.
	 */
	char *msg;
	ssize_t size;
};

struct i2c_pseudo_rsp_master_xfer {
	/* TODO: Consider replacing use of struct i2c_msg with a custom struct which replaces the flags field.  Not all flags are relevant to a controller, and the flag constants (macros) should not be required for implementing a controller. */
	/* These types match those of struct i2c_algorithm.master_xfer args. */
	struct i2c_msg *msgs;
	int num;

	/*
	 * Always initialize fields below here to zero.  They are for internal
	 * use by i2c_pseudo_rsp_master_xfer_formatter().
	 */
	int num_msgs_done;  /* type of @num field */
	size_t buf_start_plus_one;
};

/*
 * Convert int to ssize_t if it fits, else return a negative error number.
 *
 * This should be optimized away as a no-op when compiled for architectures
 * where sizeof(ssize_t) >= sizeof(int).
 */
inline static ssize_t i2c_pseudo_int_to_ssize_t(int value)
{
	if (value < SSIZE_T_MIN)
		return -ENOTRECOVERABLE;
	if (value > SSIZE_T_MAX)
		return -EOVERFLOW;
	return value;
}

/*
 * Convert ssize_t to int if it fits, else return a negative error number.
 *
 * This should be optimized away as a no-op when compiled for architectures
 * where sizeof(int) >= sizeof(ssize_t).
 */
inline static int i2c_pseudo_ssize_t_to_int(ssize_t value)
{
	if (value < INT_MIN)
		return -ENOTRECOVERABLE;
	if (value > INT_MAX)
		return -EOVERFLOW;
	return value;
}

/*
 * Convert long to ssize_t if it fits, else return a negative error number.
 *
 * This should be optimized away as a no-op when compiled for architectures
 * where sizeof(ssize_t) >= sizeof(long).
 */
inline static ssize_t i2c_pseudo_long_to_ssize_t(long value)
{
	if (value < SSIZE_T_MIN)
		return -ENOTRECOVERABLE;
	if (value > SSIZE_T_MAX)
		return -EOVERFLOW;
	return value;
}

/*
 * Convert long to int if it fits, else return a negative error number.
 *
 * This should be optimized away as a no-op when compiled for architectures
 * where sizeof(int) == sizeof(long).
 */
inline static int i2c_pseudo_long_to_int(long value)
{
	if (value < INT_MIN)
		return -ENOTRECOVERABLE;
	if (value > INT_MAX)
		return -EOVERFLOW;
	return value;
}

/* TODO: Consider moving vanprintf() and anprintf() to lib/vsprintf.c */
/*
 * vanprintf - Format a string and place it into a newly allocated buffer.
 * @out: Address of the pointer to place the buffer address into.  Will only be
 *     written to with a successful positive return value.
 * @max_size: If non-negative, the maximum buffer size that this function will
 *     attempt to allocate.  If the formatted string including trailing null
 *     character would not fit, no buffer will be allocated, and an error will
 *     be returned.  (Thus max_size of 0 will always result in an error.)
 * @fmt: The format string to use.
 * @args0: Arguments for the format string.
 *
 * Return value meanings:
 *
 *   >=0: A buffer of this size was allocated and its address written to *out.
 *        The caller now owns the buffer and is responsible for freeing it with
 *        kfree().  The final character in the buffer, not counted in this
 *        return value, is the trailing null.  This is the same return value
 *        meaning as snprintf(3).
 *
 *    <0: An error occurred.  Negate the return value for the error number.
 *        @out will not have been written to.  Errors that might come from
 *        snprintf(3) may come from this function as well.  Additionally, the
 *        following errors may occur from this function:
 *
 *        ERANGE: A buffer larger than @max_size would be needed to fit the
 *        formatted string including its trailing null character.
 *
 *        ENOMEM: Allocation of the output buffer failed.
 *
 *        ENOTRECOVERABLE: An unexpected condition occurred.  This may indicate
 *        a bug.
 */
static ssize_t vanprintf(char **out, ssize_t max_size, const char *fmt,
	va_list args0)
{
	int ret;
	ssize_t buf_size;
	char *buf = NULL;
	va_list args1;

	va_copy(args1, args0);
	ret = vsnprintf(NULL, 0, fmt, args0);
	if (ret < 0) {
		pr_err("%s: Formatting failed with error %d.\n", __func__,
			-ret);
		goto fail_before_args1;
	}
	if (max_size >= 0 && ret > max_size) {
		ret = -ERANGE;
		goto fail_before_args1;
	}

	buf_size = ret + 1;
	buf = kzalloc(buf_size, GFP_KERNEL);
	if (buf == NULL) {
		pr_err("%s: kzalloc(%zd, GFP_KERNEL) returned NULL\n", __func__,
			buf_size);
		ret = -ENOMEM;
		goto fail_before_args1;
	}

	ret = vsnprintf(buf, buf_size, fmt, args1);
	va_end(args1);
	if (ret < 0) {
		pr_err("%s: Second formatting pass produced error %d after the "
			"first pass succeeded.  This is a bug.\n", __func__,
			-ret);
		goto fail_after_args1;
	}
	if (ret + 1 != buf_size) {
		pr_err("%s: Second formatting pass produced a different "
			"formatted output size than the first.  This is a "
			"bug.  Will return -ENOTRECOVERABLE.  "
			"first_sans_null=%zd second_sans_null=%d\n", __func__,
			buf_size - 1, ret);
		ret = -ENOTRECOVERABLE;
		goto fail_after_args1;
	}

	if (ret < 0) {
		pr_err("%s: Reached success section with negative return value "
			"%d set.  This is a bug.  Will _not_ return an output "
			"buffer in *out.\n", __func__, ret);
		goto fail_after_args1;
	}
	*out = buf;
	goto just_return;

 fail_before_args1:
	va_end(args1);
 fail_after_args1:
	kfree(buf);
	if (ret >= 0) {
		pr_err("%s: Jumped to failure cleanup section with "
			"non-negative return value %d set.  This is a bug.  "
			"Will return -ENOTRECOVERABLE instead.\n", __func__,
			ret);
		ret = -ENOTRECOVERABLE;
	}

 just_return:
	return ret;
}

/*
 * anprintf - Format a string and place it into a newly allocated buffer.
 * @out: Address of the pointer to place the buffer address into.  Will only be
 *     written to with a successful positive return value.
 * @max_size: If non-negative, the maximum buffer size that this function will
 *     attempt to allocate.  If the formatted string including trailing null
 *     character would not fit, no buffer will be allocated, and an error will
 *     be returned.  (Thus max_size of 0 will always result in an error.)
 * @fmt: The format string to use.
 * @...: Arguments for the format string.
 *
 * Return value meanings:
 *
 *   >=0: A buffer of this size was allocated and its address written to *out.
 *        The caller now owns the buffer and is responsible for freeing it with
 *        kfree().  The final character in the buffer, not counted in this
 *        return value, is the trailing null.  This is the same return value
 *        meaning as snprintf(3).
 *
 *    <0: An error occurred.  Negate the return value for the error number.
 *        @out will not have been written to.  Errors that might come from
 *        snprintf(3) may come from this function as well.  Additionally, the
 *        following errors may occur from this function:
 *
 *        ERANGE: A buffer larger than @max_size would be needed to fit the
 *        formatted string including its trailing null character.
 *
 *        ENOMEM: Allocation of the output buffer failed.
 *
 *        ENOTRECOVERABLE: An unexpected condition occurred.  This may indicate
 *        a bug.
 */
static ssize_t anprintf(char **out, ssize_t max_size, const char *fmt, ...)
{
	ssize_t ret;
	va_list args;

	va_start(args, fmt);
	ret = vanprintf(out, max_size, fmt, args);
	va_end(args);
	return ret;
}

static ssize_t i2c_pseudo_rsp_buffer_formatter(void *data, char **out)
{
	struct i2c_pseudo_rsp_buffer *rsp_buf;
	rsp_buf = data;
	if (rsp_buf->buf != NULL) {
		if (rsp_buf->size > 0) {
			*out = rsp_buf->buf;
			rsp_buf->buf = NULL;
			return rsp_buf->size;
		}
		kfree(rsp_buf->buf);
	}
	kfree(rsp_buf);
	return 0;
}

/* TODO: Delete struct i2c_pseudo_rsp_error and i2c_pseudo_rsp_error_formatter().  Instead place pre-formatted error messages in struct i2c_pseudo_rsp_buffer for use with i2c_pseudo_rsp_buffer_formatter(). */
static ssize_t i2c_pseudo_rsp_error_formatter(void *data, char **out)
{
	ssize_t ret;
	char *fmt, *buf = NULL;
	struct i2c_pseudo_rsp_error *err_rsp;

	err_rsp = data;
	if (err_rsp->size == 0) {
		ret = 0;
		goto maybe_free_and_definitely_return;
	}

	if (err_rsp->size < 0) {
		ret = -ERANGE;
		goto maybe_free_and_definitely_return;
	}

	/*
	 * Error messages should not contain the null character or the
	 * controller message end character.  If the error message contains
	 * either, format it as a quoted C string.
	 */
	fmt = (memchr(err_rsp->msg, i2c_pseudo_ctrlr_end_char, err_rsp->size) ||
		(i2c_pseudo_ctrlr_end_char != '\0' &&
		 memchr(err_rsp->msg, '\0', err_rsp->size)))
		? "ERROR%c\"%*pE\""
		: "ERROR%c%*s";
	ret = anprintf(&buf, I2C_PSEUDO_MAX_MSG_BUF_SIZE, fmt,
		i2c_pseudo_ctrlr_header_sep_char, (int)err_rsp->size,
		err_rsp->msg);

	if (ret > 0) {
		*out = buf;
		err_rsp->size = 0;
	} else if (ret == 0) {
		ret = -ENOTRECOVERABLE;
		pr_debug("%s: Unexpectedly received zero formattered "
			"characeters / one buffer size (including null "
			"terminator) from anprintf(), freeing the "
			"buffer and returning %zd.\n", __func__, ret);
		kfree(buf);
	}

 maybe_free_and_definitely_return:
	if (ret <= 0) {
		kfree(err_rsp->msg);
		kfree(err_rsp);
	}
	return ret;
}

static ssize_t i2c_pseudo_rsp_master_xfer_formatter(void *data, char **out)
{
	ssize_t ret;
	size_t i, buf_size, byte_start, byte_limit;
	char *buf_start, *buf_pos;
	struct i2c_pseudo_rsp_master_xfer *mxfer_rsp;
	struct i2c_msg *i2c_msg;

	mxfer_rsp = data;

	if (mxfer_rsp->num_msgs_done < 0) {
		pr_err("%s: mxfer_rsp->num_msgs_done is negative (%d).  This "
			"is a bug, and may result in a memory leak or in freed "
			"memory being used.\n", __func__,
			mxfer_rsp->num_msgs_done);
		mxfer_rsp->num_msgs_done = 0;
		ret = -ENOTRECOVERABLE;
		goto early_error;
	}

	if (mxfer_rsp->msgs == NULL) {
		++mxfer_rsp->num_msgs_done;
		ret = 0;
		goto early_error;
	}

	if (mxfer_rsp->num_msgs_done >= mxfer_rsp->num) {
		ret = 0;
		goto definitely_free_and_definitely_return;
	}

	i2c_msg = &mxfer_rsp->msgs[mxfer_rsp->num_msgs_done];

	/*
	 * If this is a read, or if this is a write and we've finished writing
	 * the data buffer, we are done with this i2c_msg.
	 */
	if (mxfer_rsp->buf_start_plus_one >= 1 &&
	    (i2c_msg->flags & I2C_M_RD ||
	     mxfer_rsp->buf_start_plus_one >= (size_t)i2c_msg->len + 1)) {
		++mxfer_rsp->num_msgs_done;
		mxfer_rsp->buf_start_plus_one = 0;
		ret = 0;
		goto maybe_free_and_definitely_return;
	}

	if (mxfer_rsp->buf_start_plus_one <= 0) {
		/*
		 * The length is not strictly necessary with the explicit
		 * end-of-message marker (i2c_pseudo_ctrlr_end_char), however it
		 * serves as a useful sanity check for controllers to verify
		 * that no bytes were lost in kernel->userspace transmission.
		 */
		ret = anprintf(&buf_start, I2C_PSEUDO_MAX_MSG_BUF_SIZE,
			"%*s%c0x%04X%c0x%04X%c%u",
			(int)STRLEN(I2C_PSEUDO_MXFER_REQ_CMD),
			I2C_PSEUDO_MXFER_REQ_CMD,
			i2c_pseudo_ctrlr_header_sep_char, i2c_msg->addr,
			i2c_pseudo_ctrlr_header_sep_char, i2c_msg->flags,
			i2c_pseudo_ctrlr_header_sep_char, i2c_msg->len);
		if (ret > 0) {
			*out = buf_start;
			mxfer_rsp->buf_start_plus_one = 1;
		/*
		 * If we have a zero return value, it means the output buffer
		 * was allocated as size one, containing only a terminating null
		 * character.  This would be a bug given the requested format
		 * string above.  Also, formatter functions must not mutate *out
		 * when returning zero.  So if this matches, free the useless
		 * buffer and return an error.
		 */
		} else if (ret == 0) {
			ret = -ENOTRECOVERABLE;
			pr_err("%s: Unexpectedly received zero formattered "
				"characeters / one buffer size (including null "
				"terminator) from anprintf(), freeing the "
				"buffer and returning %zd.\n", __func__, ret);
			kfree(buf_start);
		}
		goto maybe_free_and_definitely_return;
	}

	byte_start = mxfer_rsp->buf_start_plus_one - 1;
	byte_limit = min(i2c_msg->len - byte_start,
		(size_t)(I2C_PSEUDO_MAX_MSG_BUF_SIZE / 3));
	/* 3 chars per byte == 2 chars for hex + 1 char for separator */
	buf_size = byte_limit * 3;
	if ((ssize_t)buf_size <= 0) {
		pr_err("%s: Calculated implausible buffer size %zu for data "
			"bytes, returning early.\n", __func__, buf_size);
		ret = buf_size ? -E2BIG : 0;
		goto maybe_free_and_definitely_return;
	}

	buf_start = kzalloc(buf_size, GFP_KERNEL);
	if (buf_start == NULL) {
		pr_err("%s: kzalloc(%zu, GFP_KERNEL) returned NULL\n", __func__,
			buf_size);
		ret = -ENOMEM;
		goto maybe_free_and_definitely_return;
	}

	for (buf_pos = buf_start, i = 0; i < byte_limit; ++i) {
		*buf_pos++ = (i || byte_start) ? i2c_pseudo_ctrlr_data_sep_char
			: i2c_pseudo_ctrlr_header_sep_char;
		buf_pos = hex_byte_pack_upper(buf_pos,
			i2c_msg->buf[byte_start + i]);
	}
	*out = buf_start;
	ret = buf_size;
	mxfer_rsp->buf_start_plus_one += i;

 maybe_free_and_definitely_return:
#if 0
	/*
	 * If this is a read, or if this is a write and we've finished writing
	 * the data buffer, we are done with this i2c_msg.
	 */
	if (i2c_msg->flags & I2C_M_RD ||
	           mxfer_rsp->buf_start_plus_one - 1 >= i2c_msg->len) {
		pr_debug("%s: Incrementing mxfer_rsp->num_msgs_done and "
			"resetting mxfer_rsp->buf_start_plus_one.  "
			"(i2c_msg->flags & I2C_M_RD)=%u "
			"mxfer_rsp->buf_start_plus_one=%zu i2c_msg->len=%u\n",
			__func__, (unsigned int)(i2c_msg->flags & I2C_M_RD),
			mxfer_rsp->buf_start_plus_one,
			(unsigned int)i2c_msg->len);
		++mxfer_rsp->num_msgs_done;
		mxfer_rsp->buf_start_plus_one = 0;
	}
#endif
	if (ret <= 0) {
 early_error:
		if (mxfer_rsp->num_msgs_done >= mxfer_rsp->num) {
 /* Jump point for when we already know the conditions above are true. */
 definitely_free_and_definitely_return:
			kfree(mxfer_rsp->msgs);
			kfree(mxfer_rsp);
		/*
		 * If we are returning an error but have not consumed all of
		 * mxfer_rsp yet, we must not attempt to output any more I2C
		 * messages from the same mxfer_rsp.  Setting mxfer_rsp->msgs to
		 * NULL tells the remaining invocations with this mxfer_rsp to
		 * output nothing.
		 */
		} else if (ret < 0 && mxfer_rsp->msgs != NULL) {
			kfree(mxfer_rsp->msgs);
			mxfer_rsp->msgs = NULL;
		}
	}
	return ret;
}

static int i2c_pseudo_cmd_xfer_reply_data_creator(void **data)
{
	struct i2c_pseudo_cmd_mxfer_reply_data *cmd_data;
	cmd_data = kzalloc(sizeof(*cmd_data), GFP_KERNEL);
	if (cmd_data == NULL) {
		pr_err("%s: kzalloc(%zu, GFP_KERNEL) returned NULL\n", __func__,
			sizeof(*cmd_data));
		return -ENOMEM;
	}
	mutex_init(&cmd_data->reply_queue_lock);
	INIT_LIST_HEAD(&cmd_data->reply_queue_head);
	*data = cmd_data;
	return 0;
}

static void i2c_pseudo_cmd_xfer_reply_data_destroyer(void *data)
{
	/* TODO: Verify (by code flow, not by writing code here) that the reply_queue_head list cannot be non-empty and that the reply_queue_lock cannot be held.  Document in a command here why those non-codified assertions are true. */
	kfree(data);
}

static int i2c_pseudo_cmd_mxfer_reply_header_receiver(void *data, char *in,
	size_t in_size, bool non_blocking)
{
	int i, int_val, ret;
	u16 u16_val;
	struct i2c_pseudo_cmd_mxfer_reply_data *cmd_data;
	struct i2c_pseudo_cmd_mxfer_reply *mxfer_reply;

	cmd_data = data;

	if (mutex_lock_killable(&cmd_data->reply_queue_lock)) {
		pr_warn("%s: cmd_data->reply_queue_lock acquisition "
			"interrupted by signal, returning -EINTR.\n", __func__);
		return -EINTR;
	}
	mxfer_reply = list_empty(&cmd_data->reply_queue_head) ? NULL :
		list_first_entry(&cmd_data->reply_queue_head,
			struct i2c_pseudo_cmd_mxfer_reply, reply_queue_item);
	if (mxfer_reply != NULL)
		ret = mutex_lock_killable(&mxfer_reply->lock);
	mutex_unlock(&cmd_data->reply_queue_lock);
	if (mxfer_reply == NULL)
		return -EIO;
	if (ret) {
		pr_warn("%s: mxfer_reply->lock acquisition interrupted by "
			"signal, returning -EINTR.\n", __func__);
		return -EINTR;
	}

	switch (mxfer_reply->state) {
	case I2C_PSEUDO_CMD_MXFER_REPLY_STATE_CMD_NEXT:
		if (STRLEN(I2C_PSEUDO_MXFER_REPLY_CMD) != in_size ||
		    memcmp(I2C_PSEUDO_MXFER_REPLY_CMD, in, in_size)) {
			/* Reaching here is a bug. */
			ret = -ENOTRECOVERABLE;
			goto unlock_and_return;
		}
		/* Expect the addr header field next. */
		mxfer_reply->state = I2C_PSEUDO_CMD_MXFER_REPLY_STATE_ADDR_NEXT;
		ret = 0;
		goto unlock_and_return;
	case I2C_PSEUDO_CMD_MXFER_REPLY_STATE_ADDR_NEXT:
	case I2C_PSEUDO_CMD_MXFER_REPLY_STATE_FLAGS_NEXT:
	case I2C_PSEUDO_CMD_MXFER_REPLY_STATE_ERRNO_NEXT:
		break;
	default:
		/* Reaching here is a bug. */
		ret = -ENOTRECOVERABLE;
		pr_err("%s: mxfer_reply->state is unexpectedly %d.  "
			"This is a bug.  Will return %d.\n", __func__,
			mxfer_reply->state, ret);
		goto unlock_and_return;
	}

	if (memchr(in, '\0', in_size)) {
		ret = -EINVAL;
		goto unlock_and_return;
	}

	ret = mxfer_reply->state == I2C_PSEUDO_CMD_MXFER_REPLY_STATE_ERRNO_NEXT
		? kstrtoint(in, 0, &int_val)
		: kstrtou16(in, 0, &u16_val);
	if (ret < 0)
		goto unlock_and_return;

	switch (mxfer_reply->state) {
	case I2C_PSEUDO_CMD_MXFER_REPLY_STATE_ADDR_NEXT:
		mxfer_reply->current_addr = u16_val;
		mxfer_reply->state =
			I2C_PSEUDO_CMD_MXFER_REPLY_STATE_FLAGS_NEXT;
		ret = 0;
		goto unlock_and_return;
	case I2C_PSEUDO_CMD_MXFER_REPLY_STATE_FLAGS_NEXT:
		for (i = mxfer_reply->current_msg_idx;
		     i < mxfer_reply->num_msgs; ++i)
			if ((mxfer_reply->msgs[i].addr ==
			     mxfer_reply->current_addr) &&
			    mxfer_reply->msgs[i].flags == u16_val)
				break;

		if (i >= mxfer_reply->num_msgs)
			goto set_invalid_and_return;

		mxfer_reply->current_msg_idx = i;
		mxfer_reply->state =
			I2C_PSEUDO_CMD_MXFER_REPLY_STATE_ERRNO_NEXT;
		goto unlock_and_return;
	case I2C_PSEUDO_CMD_MXFER_REPLY_STATE_ERRNO_NEXT:
		if (int_val)
			/*
			 * Drop the specific errno for now.  The Linux I2C API
			 * does not provide a way to return an errno for a
			 * specific message within a master_xfer() call.
			 */
			goto set_invalid_and_return;
		mxfer_reply->state = I2C_PSEUDO_CMD_MXFER_REPLY_STATE_DATA_NEXT;
		/*
		 * Ask for data bytes in multiples of 3.  Expected format is
		 * hexadecimal NN:NN:... e.g. "3C:05:F1:01" is a possible 4 byte
		 * data value.
		 */
		ret = 3;
		goto unlock_and_return;
	default:
		/* Reaching here is a bug. */
		ret = -ENOTRECOVERABLE;
		pr_err("%s: mxfer_reply->state is %d, expected %d.  "
			"This is a bug.  Will return %d.\n", __func__,
			mxfer_reply->state,
			I2C_PSEUDO_CMD_MXFER_REPLY_STATE_FLAGS_NEXT, ret);
	}

 set_invalid_and_return:
	/*
	 * Quietly ignore reads from I2C slave addresses that we did not
	 * expect to read from, and quietly ignore any extra reads if
	 * we've received all the reads we expected.
	 */
	mxfer_reply->state = I2C_PSEUDO_CMD_MXFER_REPLY_STATE_INVALID;
	/*
	 * Ask for data bytes in multiples of 1, i.e. no boundary
	 * requirements, because the we're just going to discard it.
	 * The next field could even be a header instead of data, but it
	 * doesn't matter, we're going to continue discarding the write
	 * input until the end of this write command.
	 */
	ret = 1;

 unlock_and_return:
	mutex_unlock(&mxfer_reply->lock);
	return ret;
}

static int i2c_pseudo_cmd_mxfer_reply_data_receiver(void *data, char *in,
	size_t in_size, bool non_blocking)
{
	int ret;
	char u8_hex[3] = {0};
	struct i2c_pseudo_cmd_mxfer_reply_data *cmd_data;
	struct i2c_pseudo_cmd_mxfer_reply *mxfer_reply;
	struct i2c_msg *i2c_msg;

	cmd_data = data;

	if (mutex_lock_killable(&cmd_data->reply_queue_lock)) {
		pr_warn("%s: cmd_data->reply_queue_lock acquisition "
			"interrupted by signal, returning -EINTR.\n", __func__);
		return -EINTR;
	}
	mxfer_reply = list_empty(&cmd_data->reply_queue_head) ? NULL :
		list_first_entry(&cmd_data->reply_queue_head,
			struct i2c_pseudo_cmd_mxfer_reply, reply_queue_item);
	if (mxfer_reply != NULL)
		ret = mutex_lock_killable(&mxfer_reply->lock);
	mutex_unlock(&cmd_data->reply_queue_lock);
	if (mxfer_reply == NULL)
		return -EIO;
	if (ret) {
		pr_warn("%s: mxfer_reply->lock acquisition interrupted by "
			"signal, returning -EINTR.\n", __func__);
		return -EINTR;
	}

	if (mxfer_reply->state == I2C_PSEUDO_CMD_MXFER_REPLY_STATE_INVALID) {
		ret = 0;
		goto unlock_and_return;
	}

	if (mxfer_reply->state != I2C_PSEUDO_CMD_MXFER_REPLY_STATE_DATA_NEXT ||
	    mxfer_reply->current_msg_idx < 0 ||
	    mxfer_reply->current_msg_idx >= mxfer_reply->num_msgs) {
		/* Reaching here is a bug. */
		ret = -ENOTRECOVERABLE;
		goto unlock_and_return;
	}

	i2c_msg = &mxfer_reply->msgs[mxfer_reply->current_msg_idx];

	if (!(i2c_msg->flags & I2C_M_RD)) {
		/* The controller responded to a write with data. */
		ret = -EIO;
		goto unlock_and_return;
	}

	if (i2c_msg->flags & I2C_M_RECV_LEN) {
		/*
		 * When I2C_M_RECV_LEN is set, struct i2c_algorithm.master_xfer
		 * is expected to increment struct i2c_msg.len by the actual
		 * amount of bytes read.
		 *
		 * Given the above, an initial struct i2c_msg.len value of 0
		 * would be reasonable, since it will be incremented for each
		 * byte read.
		 *
		 * An initial value of 1 representing the expected size byte
		 * also makes sense, and appears to be common practice.
		 *
		 * We consider a larger initial value to indicate a bug in the
		 * I2C/SMBus client, because it's difficult to reconcile such a
		 * value with the documented requirement that struct i2c_msg.len
		 * be "incremented by the number of block data bytes received."
		 * Besides returning an error, our only options would be to
		 * ignore and blow away a value that was potentially meaningful
		 * to the client (e.g. if it indicates the maximum buffer size),
		 * assume the value is the buffer size or expected read size
		 * (which would conflict with the documentation), or just
		 * blindly increment it, leaving it at a value greater than the
		 * actual number of bytes we wrote to the buffer, and likely
		 * indicating a size larger than the actual buffer allocation.
		 */
		if (mxfer_reply->current_buf_idx == 0) {
			if (i2c_msg->len > 1) {
				ret = -EINVAL;
				pr_warn("%s: I2C_M_RECV_LEN is set with initial"
					" struct i2c_msg.len=%u, expected <= 0."
					"  Will return %d now.\n", __func__,
					(unsigned int)i2c_msg->len, ret);
				goto unlock_and_return;
			}
			/*
			 * Subtract the read size byte because the in_size
			 * increment in the loop below will re-add it.
			 */
			i2c_msg->len = 0;
		}
	}

	while (in_size > 0 && mxfer_reply->current_buf_idx < i2c_msg->len) {
		if (in_size < 1) {
			/* Reaching here is a bug in this module. */
			ret = -ENOTRECOVERABLE;
			goto unlock_and_return;
		}
		if (in_size < 2 ||
		    (in_size > 2 && in[2] != i2c_pseudo_ctrlr_data_sep_char) ||
		    memchr(in, '\0', 2)) {
			/*
			 * Reaching here is a bug in the userspace I2C pseudo
			 * adapter controller.  (Or possibly a bug in this
			 * module itself, of course.)
			 */
			ret = -EIO;
			goto unlock_and_return;
		}
		/*
		 * When using I2C_M_RECV_LEN, the buffer is required to be able
		 * to hold:
		 *
		 * I2C_SMBUS_BLOCK_MAX
		 * +1 byte for the read size (first byte)
		 * +1 byte for the optional PEC byte (last byte if present).
		 *
		 * If reading the next byte would exceed that, return EPROTO
		 * error per Documentation/i2c/fault-codes .
		 */
		if (i2c_msg->flags & I2C_M_RECV_LEN &&
		    i2c_msg->len >= I2C_SMBUS_BLOCK_MAX + 2) {
			ret = -EPROTO;
			goto unlock_and_return;
		}
		/* Use u8_hex to get a terminating null byte for kstrtou8(). */
		memcpy(u8_hex, in, 2);
		/* TODO: Do we need to do anything different based on value of I2C_M_DMA_SAFE bit?  Do we ever or always need to use copy_to_user(), possibly based on I2C_M_DMA_SAFE bit? */
		ret = kstrtou8(u8_hex, 16,
			&i2c_msg->buf[mxfer_reply->current_buf_idx]);
		if (ret < 0)
			goto unlock_and_return;
		if (i2c_msg->flags & I2C_M_RECV_LEN)
			++i2c_msg->len;
		++mxfer_reply->current_buf_idx;
		in += min((size_t)3, in_size);
		in_size -= min((size_t)3, in_size);
	}

	/* Quietly ignore any bytes beyond the buffer size. */
	ret = 0;

 unlock_and_return:
	mutex_unlock(&mxfer_reply->lock);
	return ret;
}

static int i2c_pseudo_cmd_mxfer_reply_cmd_completer(void *data,
	int receive_status, bool non_blocking)
{
	int ret;
	struct i2c_pseudo_cmd_mxfer_reply_data *cmd_data;
	struct i2c_pseudo_cmd_mxfer_reply *mxfer_reply;
	struct i2c_msg *i2c_msg;

	cmd_data = data;

	if (mutex_lock_killable(&cmd_data->reply_queue_lock)) {
		pr_warn("%s: cmd_data->reply_queue_lock acquisition "
			"interrupted by signal, returning -EINTR.\n", __func__);
		return -EINTR;
	}
	mxfer_reply = list_empty(&cmd_data->reply_queue_head) ? NULL :
		list_first_entry(&cmd_data->reply_queue_head,
			struct i2c_pseudo_cmd_mxfer_reply, reply_queue_item);
	if (mxfer_reply != NULL) {
		ret = mutex_lock_killable(&mxfer_reply->lock);
		if (!ret &&
		    mxfer_reply->current_msg_idx >= mxfer_reply->num_msgs) {
			list_del_init(&mxfer_reply->reply_queue_item);
			--cmd_data->reply_queue_length;
		}
	}
	mutex_unlock(&cmd_data->reply_queue_lock);
	if (mxfer_reply == NULL)
		return -EIO;
	if (ret) {
		pr_warn("%s: mxfer_reply->lock acquisition interrupted by "
			"signal, returning -EINTR.\n", __func__);
		return -EINTR;
	}

	i2c_msg = &mxfer_reply->msgs[mxfer_reply->current_msg_idx];

	if (!receive_status &&
	    mxfer_reply->state == I2C_PSEUDO_CMD_MXFER_REPLY_STATE_DATA_NEXT &&
	    (!(i2c_msg->flags & I2C_M_RD) ||
	     mxfer_reply->current_buf_idx >= i2c_msg->len))
		++mxfer_reply->ret;

	/*
	 * If we got to the point of receiving data, consider ourselves done
	 * with the current i2c_msg, even if an error occurred.
	 */
	if (mxfer_reply->state == I2C_PSEUDO_CMD_MXFER_REPLY_STATE_DATA_NEXT)
		++mxfer_reply->current_msg_idx;

	mxfer_reply->state = I2C_PSEUDO_CMD_MXFER_REPLY_STATE_CMD_NEXT;
	mxfer_reply->current_addr = 0;
	mxfer_reply->current_buf_idx = 0;

	if (mxfer_reply->current_msg_idx >= mxfer_reply->num_msgs)
		complete_all(&mxfer_reply->data_filled);

	mutex_unlock(&mxfer_reply->lock);
	return I2C_PSEUDO_CMD_COMPLETER_SUCCESS;
}

static int i2c_pseudo_cmd_adap_shutdown_header_receiver(void *data, char *in,
	size_t in_size, bool non_blocking)
{
	if (STRLEN(I2C_PSEUDO_MXFER_REPLY_CMD) != in_size ||
	    memcmp(I2C_PSEUDO_MXFER_REPLY_CMD, in, in_size)) {
		/* Reaching here is a bug. */
		return -ENOTRECOVERABLE;
	}
	/*
	 * No more header fields or data are expected.  This directs any further
	 * input in this command to the data_receiver, which for this write
	 * command will unconditionally indicate a controller error. */
	return 1;
}

static int i2c_pseudo_cmd_adap_shutdown_data_receiver(void *data, char *in,
	size_t in_size, bool non_blocking)
{
	/*
	 * Reaching here means the controller wrote extra data in the adapter
	 * shutdown command line after the initial command name.  That is
	 * unexpected and indicates a controller bug.
	 */
	return -EINVAL;
}

static int i2c_pseudo_cmd_adap_shutdown_cmd_completer(void *data,
	int receive_status, bool non_blocking)
{
	/* Refuse to shutdown if there were errors processing this command. */
	if (receive_status)
		return I2C_PSEUDO_CMD_COMPLETER_SUCCESS;
	return I2C_PSEUDO_CMD_COMPLETER_SHUTDOWN;
}

static const struct i2c_pseudo_cmd i2c_pseudo_cmds[] = {
	{
		.cmd_string = I2C_PSEUDO_MXFER_REPLY_CMD,
		.cmd_size = STRLEN(I2C_PSEUDO_MXFER_REPLY_CMD),
		.data_creator = i2c_pseudo_cmd_xfer_reply_data_creator,
		.data_destroyer = i2c_pseudo_cmd_xfer_reply_data_destroyer,
		.header_receiver = i2c_pseudo_cmd_mxfer_reply_header_receiver,
		.data_receiver = i2c_pseudo_cmd_mxfer_reply_data_receiver,
		.cmd_completer = i2c_pseudo_cmd_mxfer_reply_cmd_completer,
	},
/* TODO: Implement handling of an error write command from the controller. */
#ifdef I2C_PSEUDO_ENABLE_CTRLR_ERROR_CMD
	{
		.cmd_string = I2C_PSEUDO_CTRLR_ERROR_CMD,
		.cmd_size = STRLEN(I2C_PSEUDO_CTRLR_ERROR_CMD),
		.data_creator = i2c_pseudo_cmd_ctrlr_error_data_creator,
		.data_destroyer = i2c_pseudo_cmd_ctrlr_error_data_destroyer,
		.header_receiver = i2c_pseudo_cmd_ctrlr_error_header_receiver,
		.data_receiver = i2c_pseudo_cmd_ctrlr_error_data_receiver,
		.cmd_completer = i2c_pseudo_cmd_ctrlr_error_cmd_completer,
	},
#endif
	{
		.cmd_string = I2C_PSEUDO_ADAP_SHUTDOWN_CMD,
		.cmd_size = STRLEN(I2C_PSEUDO_ADAP_SHUTDOWN_CMD),
		.header_receiver = i2c_pseudo_cmd_adap_shutdown_header_receiver,
		.data_receiver = i2c_pseudo_cmd_adap_shutdown_data_receiver,
		.cmd_completer = i2c_pseudo_cmd_adap_shutdown_cmd_completer,
	},
};

/*
 * Each of these should have a build-time assertion in
 * i2c_pseudo_build_assertions() that enforces their correctness.
 * TODO: Find a way to actually make that so.
 */
enum {
	I2C_PSEUDO_CMD_MXFER_REPLY_IDX = 0,
#ifdef I2C_PSEUDO_ENABLE_CTRLR_ERROR_CMD
	I2C_PSEUDO_CMD_CTRLR_ERROR_IDX,
#endif
	I2C_PSEUDO_CMD_ADAP_SHUTDOWN_IDX,
	/* Keep this at the end! This should equal ARRAYLEN(i2c_pseudo_cmds). */
	I2C_PSEUDO_NUM_WRITE_CMDS,
};

/*
 * Avoid allocating this struct on the stack, it contains a large buffer as a
 * direct member.
 *
 * Locking rules:
 *
 * - To avoid deadlocks, never attempt to hold more than one of the locks in
 * this structure at once, with the following exceptions:
 *   - It is permissible to acquire read_rsp_queue_lock while holding cmd_lock.
 *   - It is permissible to acquire read_rsp_queue_lock while holding rsp_lock.
 *
 * - Never allow interruptions from non-killable signals.
 *
 * - Allow interruption from killable signals when acquiring to add a new item
 * to a pointer or data structure.  It should always be possible to fail cleanly
 * without memory leaks in those scenarios.
 *
 * - Do _not_ allow interruption when acquiring to remove an item from a pointer
 * or data structure, if the item would otherwise become leaked memory, or if
 * its removal would never be accounted for properly in a counter or such.
 */
struct i2c_pseudo_controller {
	unsigned int index;
	/* TODO: Export the controller ID in sysfs. */
	i2c_pseudo_ctrlr_id_t id;
	struct i2c_adapter i2c_adapter;

	struct mutex shutdown_lock;
	bool shutdown_indicated;

	wait_queue_head_t poll_wait_queue;

	/* This must be held while read or writing cmd_* fields. */
	struct mutex cmd_lock;
	/*
	 * This becomes the @receive_status arg to
	 * struct i2c_pseudo_cmd.cmd_completer callback.
	 *
	 * A negative value is an error number from
	 * struct i2c_pseudo_cmd.header_receiver or
	 * struct i2c_pseudo_cmd.data_receiver.
	 *
	 * A zero value means no error has occurred so far in processing the
	 * current write reply command.
	 *
	 * A positive value is an error number from a non-command-specific part
	 * of write command processing, e.g. from the
	 * struct file_operations.write callback itself, or function further up
	 * its call stack that is not specific to any particular write command.
	 */
	int cmd_receive_status;
	/*
	 * Index of i2c_pseudo_cmds[] and .cmd_data[] plus one, i.e. value of 1
	 * means 0 index.  Value of 0 (zero) means the controller is waiting for
	 * a new command.
	 */
	int cmd_idx_plus_one;
	int cmd_data_increment;
	size_t cmd_size;
	char cmd_buf[I2C_PSEUDO_CTRLR_CMD_BUF_SIZE];
	void *cmd_data[ARRAYLEN(i2c_pseudo_cmds)];

	struct completion read_rsp_queued;
	/* This must be held while read or writing read_rsp_queue_* fields. */
	struct mutex read_rsp_queue_lock;
	/*
	 * This is a FIFO queue of struct i2c_pseudo_rsp.queue .
	 *
	 * This MUST be strictly used as FIFO.  Only consume or pop the first
	 * item.  Only append to the end.  Users of this queue assume this FIFO
	 * behavior is strictly followed, and their uses of read_rsp_queue_lock
	 * would not be safe otherwise.
	 */
	struct list_head read_rsp_queue_head;
	unsigned int read_rsp_queue_length;

	/* This must be held while read or writing rsp_* fields. */
	struct mutex rsp_lock;
	bool rsp_invalidated;
	/*
	 * Holds formatted string from most recently popped item of
	 * read_rsp_queue_head if it was not wholly consumed by the last
	 * controller read.
	 */
	char *rsp_buf_start;
	char *rsp_buf_pos;
	ssize_t rsp_buf_remaining;
};

static int i2c_pseudo_adap_is_shutdown(struct i2c_pseudo_controller *pdata)
{
	int ret;

	if (mutex_lock_killable(&pdata->shutdown_lock)) {
		ret = -EINTR;
		pr_warn("%s: I2C adapter %d shutdown_data->lock acquisition "
			"interrupted by signal, returning %d.\n",
			__func__, pdata->i2c_adapter.nr, ret);
		goto just_return;
	}
	ret = !!pdata->shutdown_indicated;
	mutex_unlock(&pdata->shutdown_lock);

 just_return:
	return ret;
}

static int i2c_pseudo_adap_set_shutdown(struct i2c_pseudo_controller *pdata)
{
	int ret;

	if (mutex_lock_killable(&pdata->shutdown_lock)) {
		ret = -EINTR;
		pr_warn("%s: I2C adapter %d shutdown_data->lock acquisition "
			"interrupted by signal, returning %d.\n",
			__func__, pdata->i2c_adapter.nr, ret);
		goto just_return;
	}
	pdata->shutdown_indicated = true;
	mutex_unlock(&pdata->shutdown_lock);

	complete_all(&pdata->read_rsp_queued);
	wake_up_interruptible_all(&pdata->poll_wait_queue);
	ret = 0;

 just_return:
	return ret;
}

/* Must be called with pdata->rsp_lock held. */
inline static bool i2c_pseudo_poll_in(struct i2c_pseudo_controller *pdata)
{
	return pdata->rsp_invalidated || pdata->rsp_buf_remaining != 0 ||
		!list_empty(&pdata->read_rsp_queue_head);
}

inline static int i2c_pseudo_fill_rsp_buf(struct i2c_pseudo_rsp *rsp_wrapper,
	struct i2c_pseudo_rsp_buffer *rsp_buf, char *contents, size_t size)
{
	rsp_buf->buf = kmemdup(contents, size, GFP_KERNEL);
	if (rsp_buf->buf == NULL)
		return -ENOMEM;
	rsp_buf->size = size;
	rsp_wrapper->data = rsp_buf;
	rsp_wrapper->formatter = i2c_pseudo_rsp_buffer_formatter;
	return 0;
}

#define I2C_PSEUDO_FILL_RSP_BUF_WITH_LITERAL(rsp_wrapper, rsp_buf, str_literal)\
	i2c_pseudo_fill_rsp_buf(\
		rsp_wrapper, rsp_buf, str_literal, STRLEN(str_literal))

static int i2c_pseudo_adapter_master_xfer(struct i2c_adapter *adap,
		struct i2c_msg *msgs, int num)
{
	int i, ret = 0;
	long wait_ret;
	size_t wrappers_length, wrapper_idx = 0, rsp_bufs_idx = 0;
	struct i2c_pseudo_controller *pdata;
	struct i2c_pseudo_rsp **rsp_wrappers;
	struct i2c_pseudo_rsp_buffer *rsp_bufs[2] = {0};
	struct i2c_pseudo_rsp_master_xfer *mxfer_rsp;
	struct i2c_pseudo_cmd_mxfer_reply_data *cmd_data;
	struct i2c_pseudo_cmd_mxfer_reply *mxfer_reply;

	if (num <= 0) {
		if (num < 0)
			ret = -EINVAL;
		goto just_return;
	}

	pdata = adap->algo_data;
	cmd_data = pdata->cmd_data[I2C_PSEUDO_CMD_MXFER_REPLY_IDX];

	ret = i2c_pseudo_adap_is_shutdown(pdata);
	if (ret != 0)
		goto just_return;

	wrappers_length = (size_t)num + ARRAYLEN(rsp_bufs);
	rsp_wrappers = kcalloc(wrappers_length, sizeof(*rsp_wrappers),
		GFP_KERNEL);
	if (rsp_wrappers == NULL) {
		pr_err("%s: kcalloc(%zu, %zu, GFP_KERNEL) returned NULL\n",
			__func__, wrappers_length, sizeof(*rsp_wrappers));
		ret = -ENOMEM;
		goto just_return;
	}

	mxfer_reply = kzalloc(sizeof(*mxfer_reply), GFP_KERNEL);
	if (mxfer_reply == NULL) {
		pr_err("%s: kzalloc(%zu, GFP_KERNEL) returned NULL\n", __func__,
			sizeof(*mxfer_rsp));
		ret = -ENOMEM;
		goto return_after_rsp_wrappers_ptrs_alloc;
	}

	mxfer_reply->num_msgs = num;
	init_completion(&mxfer_reply->data_filled);
	mutex_init(&mxfer_reply->lock);

	mxfer_reply->msgs = kcalloc(num, sizeof(*mxfer_reply->msgs),
		GFP_KERNEL);
	if (mxfer_reply->msgs == NULL) {
		pr_err("%s: kcalloc(%d, %zu, GFP_KERNEL) returned NULL\n",
			__func__, num, sizeof(*mxfer_reply->msgs));
		ret = -ENOMEM;
		goto return_after_mxfer_reply_alloc;
	}

	for (i = 0; i < num; ++i) {
		mxfer_reply->msgs[i].addr = msgs[i].addr;
		mxfer_reply->msgs[i].flags = msgs[i].flags;
		mxfer_reply->msgs[i].len = msgs[i].len;
		if (msgs[i].flags & I2C_M_RD)
			/* Copy the address, not the data. */
			mxfer_reply->msgs[i].buf = msgs[i].buf;
	}

	for (i = 0; i < ARRAYLEN(rsp_bufs); ++i) {
		rsp_bufs[i] = kzalloc(sizeof(*rsp_bufs[i]), GFP_KERNEL);
		if (rsp_bufs[i] == NULL) {
			pr_err("%s: kzalloc(%zu, GFP_KERNEL) for rsp_bufs[%d] "
				"returned NULL\n", __func__,
				sizeof(*rsp_bufs[i]), i);
			ret = -ENOMEM;
			goto return_after_reply_msgs_alloc;
		}
	}

	mxfer_rsp = kzalloc(sizeof(*mxfer_rsp), GFP_KERNEL);
	if (mxfer_rsp == NULL) {
		pr_err("%s: kzalloc(%zu, GFP_KERNEL) returned NULL\n", __func__,
			sizeof(*mxfer_rsp));
		ret = -ENOMEM;
		goto fail_after_individual_rsp_bufs_alloc;
	}

	mxfer_rsp->num = num;

	mxfer_rsp->msgs = kcalloc(num, sizeof(*mxfer_rsp->msgs), GFP_KERNEL);
	if (mxfer_rsp->msgs == NULL) {
		pr_err("%s: kcalloc(%d, %zu, GFP_KERNEL) returned NULL\n",
			__func__, num, sizeof(*mxfer_rsp->msgs));
		ret = -ENOMEM;
		goto fail_after_mxfer_rsp_alloc;
	}

	for (i = 0; i < num; ++i) {
		mxfer_rsp->msgs[i].addr = msgs[i].addr;
		mxfer_rsp->msgs[i].flags = msgs[i].flags;
		mxfer_rsp->msgs[i].len = msgs[i].len;
		if (msgs[i].flags & I2C_M_RD)
			continue;
		/* Copy the data, not the address. */
		mxfer_rsp->msgs[i].buf = kmemdup(msgs[i].buf, msgs[i].len,
			GFP_KERNEL);
		if (mxfer_rsp->msgs[i].buf == NULL) {
			pr_err("%s: kmemdup(msgs[%d].buf, %zu, GFP_KERNEL) "
				"returned NULL\n", __func__, i,
				(size_t)msgs[i].len);
			ret = -ENOMEM;
			goto fail_after_rsp_msgs_alloc;
		}
	}

	for (i = 0; i < wrappers_length; ++i) {
		rsp_wrappers[i] = kzalloc(sizeof(*rsp_wrappers[i]), GFP_KERNEL);
		if (rsp_wrappers[i] == NULL) {
			pr_err("%s: kzalloc(%zu, GFP_KERNEL) for "
				"rsp_wrappers[%d] returned NULL\n", __func__,
				sizeof(*rsp_wrappers[i]), i);
			ret = -ENOMEM;
			goto fail_after_individual_rsp_wrappers_alloc;
		}
	}

	ret = I2C_PSEUDO_FILL_RSP_BUF_WITH_LITERAL(rsp_wrappers[wrapper_idx++],
		rsp_bufs[rsp_bufs_idx++], I2C_PSEUDO_BEGIN_MXFER_REQ_CMD);
	if (ret < 0) {
		pr_err("%s: I2C_PSEUDO_FILL_RSP_BUF_WITH_LITERAL("
			"rsp_wrappers[wrapper_idx++], "
			"rsp_bufs[rsp_bufs_idx++], "
			"I2C_PSEUDO_BEGIN_MXFER_REQ_CMD) returned %d.\n",
			__func__, ret);
		goto fail_after_individual_rsp_wrappers_alloc;
	}

	for (i = 0; i < num; ++i) {
		rsp_wrappers[wrapper_idx]->data = mxfer_rsp;
		rsp_wrappers[wrapper_idx++]->formatter =
			i2c_pseudo_rsp_master_xfer_formatter;
	}

	ret = I2C_PSEUDO_FILL_RSP_BUF_WITH_LITERAL(rsp_wrappers[wrapper_idx++],
		rsp_bufs[rsp_bufs_idx++], I2C_PSEUDO_COMMIT_MXFER_REQ_CMD);
	if (ret < 0) {
		pr_err("%s: I2C_PSEUDO_FILL_RSP_BUF_WITH_LITERAL("
			"rsp_wrappers[wrapper_idx++], "
			"rsp_bufs[rsp_bufs_idx++], "
			"I2C_PSEUDO_COMMIT_MXFER_REQ_CMD) returned %d.\n",
			__func__, ret);
		goto fail_after_individual_rsp_wrappers_alloc;
	}

	BUILD_BUG_ON(rsp_bufs_idx != ARRAYLEN(rsp_bufs));

	if (mutex_lock_killable(&pdata->read_rsp_queue_lock)) {
		ret = -EINTR;
		pr_warn("%s: I2C adapter %d read_rsp_queue_lock acquisition "
			"interrupted by signal, returning %d.\n", __func__,
			adap->nr, ret);
		goto fail_after_individual_rsp_wrappers_alloc;
	}
	if (pdata->read_rsp_queue_length >= I2C_PSEUDO_CTRLR_RSP_QUEUE_LIMIT) {
		ret = -ENOBUFS;
		pr_debug("%s: I2C pseudo controller for I2C adapter %d has "
			"reached " STR(I2C_PSEUDO_CTRLR_RSP_QUEUE_LIMIT) " "
			"request queue limit.  Returning %d now.\n", __func__,
			adap->nr, ret);
		goto fail_with_read_rsp_queue_lock;
	}

	if (mutex_lock_killable(&cmd_data->reply_queue_lock)) {
		ret = -EINTR;
		pr_warn("%s: I2C adapter %d reply_queue_lock acquisition "
			"interrupted by signal, returning %d.\n", __func__,
			adap->nr, ret);
		goto fail_with_read_rsp_queue_lock;
	}
	if (cmd_data->reply_queue_length >= I2C_PSEUDO_CTRLR_RSP_QUEUE_LIMIT) {
		ret = -ENOBUFS;
		pr_debug("%s: I2C pseudo controller for I2C adapter %d has "
			"reached " STR(I2C_PSEUDO_CTRLR_RSP_QUEUE_LIMIT) " "
			"response queue limit.  Returning %d now.\n", __func__,
			adap->nr, ret);
		goto fail_with_reply_queue_lock;
	}

	mxfer_reply->id = cmd_data->next_mxfer_id++;
	list_add_tail(&mxfer_reply->reply_queue_item,
		&cmd_data->reply_queue_head);
	++cmd_data->reply_queue_length;

	for (i = 0; i < wrappers_length; ++i) {
		list_add_tail(&rsp_wrappers[i]->queue,
			&pdata->read_rsp_queue_head);
		complete(&pdata->read_rsp_queued);
	}
	pdata->read_rsp_queue_length += wrappers_length;

	mutex_unlock(&cmd_data->reply_queue_lock);
	mutex_unlock(&pdata->read_rsp_queue_lock);

	wake_up_interruptible(&pdata->poll_wait_queue);
	wait_ret = wait_for_completion_killable_timeout(
		&mxfer_reply->data_filled, adap->timeout);

	/* Not interruptable because we must free mxfer_reply. */
	mutex_lock(&cmd_data->reply_queue_lock);
	/*
	 * Ensure mxfer_reply is not in use before dequeuing and freeing it.
	 * This depends on the requirement that mxfer_reply->lock only be
	 * acquired while holding cmd_data->reply_queue_lock.
	 *
	 * Not interruptable because we must free mxfer_reply.
	 */
	mutex_lock(&mxfer_reply->lock);

	if (wait_ret == -ERESTARTSYS) {
		ret = -EINTR;
		pr_warn("%s: I2C adapter %d data_filled completion wait "
			"interrupted by signal, returning %d.\n", __func__,
			adap->nr, ret);
	} else if (wait_ret < 0) {
		ret = i2c_pseudo_long_to_int(wait_ret);
		pr_warn("%s: I2C adapter %d data_filled completion returned "
			"unexpected error %ld, this function will return %d "
			"now.\n", __func__, adap->nr, wait_ret, ret);
	} else {
		ret = mxfer_reply->ret;
		if (wait_ret == 0)
			pr_warn("%s: I2C adapter %d data_filled completion "
				"wait timed out after %u ms, this function "
				"will return %d from mxfer_reply->ret now.\n",
				__func__, adap->nr,
				jiffies_to_msecs(adap->timeout), ret);
		else
			pr_debug("%s: I2C adapter %d data_filled completion "
				"wait returned successfully before its %u ms "
				"timeout, this function will return %d from "
				"mxfer_reply->ret now.\n", __func__, adap->nr,
				jiffies_to_msecs(adap->timeout), ret);
	}

	/*
	 * This depends on other functions that might delete
	 * mxfer_reply->reply_queue_item from cmd_data->reply_queue_head using
	 * list_del_init(), never list_del().
	 */
	if (!list_empty(&mxfer_reply->reply_queue_item)) {
		list_del(&mxfer_reply->reply_queue_item);
		--cmd_data->reply_queue_length;
	}

	mutex_unlock(&mxfer_reply->lock);
	mutex_unlock(&cmd_data->reply_queue_lock);
	goto return_after_reply_msgs_alloc;

 fail_with_reply_queue_lock:
	mutex_unlock(&cmd_data->reply_queue_lock);
 fail_with_read_rsp_queue_lock:
	mutex_unlock(&pdata->read_rsp_queue_lock);
 fail_after_individual_rsp_wrappers_alloc:
	for (i = 0; i < wrappers_length; ++i)
		kfree(rsp_wrappers[i]);
 fail_after_rsp_msgs_alloc:
	for (i = 0; i < num; ++i)
		kfree(mxfer_rsp->msgs[i].buf);
	kfree(mxfer_rsp->msgs);
 fail_after_mxfer_rsp_alloc:
	kfree(mxfer_rsp);
 fail_after_individual_rsp_bufs_alloc:
	for (i = 0; i < ARRAYLEN(rsp_bufs); ++i) {
		kfree(rsp_bufs[i]->buf);
		kfree(rsp_bufs[i]);
	}
 return_after_reply_msgs_alloc:
	kfree(mxfer_reply->msgs);
 return_after_mxfer_reply_alloc:
	kfree(mxfer_reply);
 return_after_rsp_wrappers_ptrs_alloc:
	kfree(rsp_wrappers);
 just_return:
	return ret;
}

/* TODO: Ask the controller for its functionality, though not with the same bitmask.  Basic I2C functionality should be implied and required, but support for other functionality such as I2C_FUNC_10BIT_ADDR, I2C_FUNC_NOSTART, and I2C_FUNC_PROTOCOL_MANGLING could be optional and indicated by the controller. */
static u32 i2c_pseudo_adapter_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm i2c_pseudo_algorithm = {
	.master_xfer = i2c_pseudo_adapter_master_xfer,
	.functionality = i2c_pseudo_adapter_functionality,
};

/* i2c_pseudo_counters.lock must _not_ be held when calling this. */
static void i2c_pseudo_remove_from_counters(struct i2c_pseudo_controller *pdata)
{
	mutex_lock(&i2c_pseudo_counters.lock);
	i2c_pseudo_counters.all_controllers[pdata->index] = NULL;
	--i2c_pseudo_counters.count;
	mutex_unlock(&i2c_pseudo_counters.lock);
}

static int i2c_pseudo_cdev_open(struct inode *inodep, struct file *filep)
{
	int ret = 0;
	unsigned int i, num_cmd_data_created = 0;
	i2c_pseudo_ctrlr_id_t ctrlr_id;
	struct i2c_pseudo_controller *pdata;
	/* TODO: Export i2c_pseudo_limit in sysfs. */
	/* TODO: Export active I2C pseudo adapter count in sysfs. */

	/* I2C pseudo adapter controllers are not seekable. */
	nonseekable_open(inodep, filep);
	/* Refuse fsnotify events.  Modeled after /dev/ptmx implementation. */
	filep->f_mode |= FMODE_NONOTIFY;

	/* Allocate the I2C adapter. */
	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL) {
		pr_err("%s: kzalloc(%zu, GFP_KERNEL) returned NULL\n", __func__,
			sizeof(*pdata));
		ret = -ENOMEM;
		goto fail_beginning;
	}

	INIT_LIST_HEAD(&pdata->read_rsp_queue_head);
	init_waitqueue_head(&pdata->poll_wait_queue);
	init_completion(&pdata->read_rsp_queued);
	mutex_init(&pdata->shutdown_lock);
	mutex_init(&pdata->cmd_lock);
	mutex_init(&pdata->rsp_lock);
	mutex_init(&pdata->read_rsp_queue_lock);

	for (i = 0; i < ARRAYLEN(i2c_pseudo_cmds); ++i) {
		if (i2c_pseudo_cmds[i].data_creator == NULL)
			continue;
		ret = i2c_pseudo_cmds[i].data_creator(&pdata->cmd_data[i]);
		if (ret < 0)
			break;
	}
	num_cmd_data_created = i;
	if (ret < 0)
		goto fail_after_cmd_data_created;

	if (mutex_lock_killable(&i2c_pseudo_counters.lock)) {
		ret = -EINTR;
		pr_warn("%s: Request for new pseudo I2C adapter lock "
			"acquisition interrupted by signal, returning %d.\n",
			__func__,
			ret);
		goto fail_after_cmd_data_created;
	}
	for (i = 0; i < i2c_pseudo_limit; ++i)
		if (i2c_pseudo_counters.all_controllers[i] == NULL)
			break;
	if (i >= i2c_pseudo_limit) {
		pr_err("%s: The limit of %u pseudo I2C adapters has already "
			"been reached, cannot add another one.\n", __func__,
			i2c_pseudo_limit);
		ret = -ENOSPC;
		goto unlock_counters;
	}
	pdata->index = i;

	for (ctrlr_id = i2c_pseudo_counters.next_ctrlr_id;;) {
		for (i = 0; i < i2c_pseudo_limit; ++i) {
			if (i2c_pseudo_counters.all_controllers[i] != NULL &&
			    (i2c_pseudo_counters.all_controllers[i]->id ==
			     ctrlr_id))
				break;
		}
		if (i >= i2c_pseudo_limit) {
			pdata->id = ctrlr_id;
			i2c_pseudo_counters.next_ctrlr_id = ctrlr_id + 1;
			++i2c_pseudo_counters.count;
			i2c_pseudo_counters.all_controllers[pdata->index] =
				pdata;
			break;
		}
		if (++ctrlr_id == i2c_pseudo_counters.next_ctrlr_id) {
			pr_err("%s: Cycled through every possible I2C pseudo "
				"controller ID without finding a free one.  "
				"This is implausible, and may indicate a bug.  "
				"i2c_pseudo_counters.count=%u\n", __func__,
				i2c_pseudo_counters.count);
			ret = -ENOSPC;
			break;
		}
	}

 unlock_counters:
	mutex_unlock(&i2c_pseudo_counters.lock);
	if (ret < 0)
		/* Assume i2c_pseudo_counters was never mutated. */
		goto fail_after_cmd_data_created;

	/* Initialize the I2C adapter. */
	pdata->i2c_adapter.owner = THIS_MODULE;
	pdata->i2c_adapter.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	pdata->i2c_adapter.algo = &i2c_pseudo_algorithm;
	pdata->i2c_adapter.algo_data = pdata;
	pdata->i2c_adapter.timeout = msecs_to_jiffies(i2c_pseudo_timeout_ms);
	pdata->i2c_adapter.dev.parent = &i2c_pseudo_device;
	ret = snprintf(pdata->i2c_adapter.name, sizeof(pdata->i2c_adapter.name),
		I2C_PSEUDO_ADAPTER_PREFIX "%u", pdata->id);
	if (ret < 0) {
		pr_err("%s: snprintf() for inodep=%p filep=%p i2c_adapter.name "
			"failed with error %d\n", __func__, inodep, filep,
			-ret);
		goto fail_after_counters_update;
	}

	/* Add the I2C adapter. */
	ret = i2c_add_adapter(&pdata->i2c_adapter);
	if (ret < 0) {
		pr_err("%s: i2c_add_adapter() for inodep=%p filep=%p failed "
			"with error %d\n", __func__, inodep, filep, -ret);
		goto fail_after_counters_update;
	}

	/* Return success. */
	filep->private_data = pdata;
	ret = 0;
	goto just_return;

 fail_after_counters_update:
	i2c_pseudo_remove_from_counters(pdata);
 fail_after_cmd_data_created:
	for (i = 0; i < num_cmd_data_created; ++i)
		if (i2c_pseudo_cmds[i].data_destroyer != NULL)
			i2c_pseudo_cmds[i].data_destroyer(pdata->cmd_data[i]);
	kfree(pdata);
 fail_beginning:
	if (ret >= 0) {
		pr_err("%s: Jumped to failure cleanup section with "
			"non-negative return value %d set.  This is a bug.  "
			"Will return -ENOTRECOVERABLE instead.\n", __func__,
			ret);
		ret = -ENOTRECOVERABLE;
	}
 just_return:
	return ret;
}

static int i2c_pseudo_cdev_release(struct inode *inodep, struct file *filep)
{
	int i;
	struct i2c_pseudo_controller *pdata;

	pdata = filep->private_data;
#ifdef I2C_PSEUDO_CHECK_FOR_CDEV_RELEASE_RACE
	if (mutex_trylock(&pdata->read_rsp_queue_lock))
		mutex_unlock(&pdata->read_rsp_queue_lock);
	else
		pr_err("%s: Could not immediately acquire I2C adapter %d "
			"read_rsp_queue_lock.\n", __func__,
			pdata->i2c_adapter.nr);
	if (mutex_trylock(&pdata->cmd_lock))
		mutex_unlock(&pdata->cmd_lock);
	else
		pr_err("%s: Could not immediately acquire I2C adapter %d "
			"cmd_lock.\n", __func__, pdata->i2c_adapter.nr);
	if (mutex_trylock(&pdata->rsp_lock))
		mutex_unlock(&pdata->rsp_lock);
	else
		pr_err("%s: Could not immediately acquire I2C adapter %d "
			"rsp_lock.\n", __func__, pdata->i2c_adapter.nr);
#endif
	/*
	 * Linux guarantees there are no outstanding reads or writes when a
	 * struct file is released, so no further synchronization with the other
	 * struct file_operations callbacks is needed.
	 */
	filep->private_data = NULL;
	i2c_del_adapter(&pdata->i2c_adapter);

	for (i = 0; i < ARRAYLEN(i2c_pseudo_cmds); ++i) {
		if (i2c_pseudo_cmds[i].data_destroyer != NULL)
			i2c_pseudo_cmds[i].data_destroyer(pdata->cmd_data[i]);
		pdata->cmd_data[i] = NULL;
	}

	i2c_pseudo_remove_from_counters(pdata);
	kfree(pdata);
	return 0;
}

static ssize_t i2c_pseudo_cdev_read(struct file *filep, char __user *buf,
		size_t count, loff_t *f_ps)
{
	ssize_t ret = 0, copy_size;
	unsigned long copy_ret;
	int int_ret;
	long wait_ret;
	bool non_blocking;
	struct i2c_pseudo_controller *pdata;
	struct i2c_pseudo_rsp *rsp_wrapper = NULL;

	/*
	 * Just in case this could change out from under us, best to keep a
	 * consistent view for the duration of this syscall.
	 */
	non_blocking = !!(filep->f_flags & O_NONBLOCK);
	pdata = filep->private_data;

	/* Never process more than we can indicate in the return value. */
	if (count > (size_t)SSIZE_T_MAX)
		count = SSIZE_T_MAX;

	/*
	 * Since read() calls are effectively serialized by way of
	 * pdata->rsp_lock, we MUST NOT block on obtaining that lock if in
	 * non-blocking mode, because it might be held by a blocking read().
	 */
	if (non_blocking) {
		if (!mutex_trylock(&pdata->rsp_lock)) {
			BUILD_BUG_ON(ret != 0);
			ret = -EAGAIN;
			pr_debug("%s: Could not immediately acquire I2C adapter "
				"%d rsp_lock for O_NONBLOCK read, returning "
				"%zd.\n", __func__, pdata->i2c_adapter.nr, ret);
			goto just_return;
		}
	} else {
		if (mutex_lock_killable(&pdata->rsp_lock)) {
			BUILD_BUG_ON(ret != 0);
			ret = -EINTR;
			pr_warn("%s: I2C adapter %d rsp_lock acquisition "
				"interrupted by signal, returning %zd.\n",
				__func__, pdata->i2c_adapter.nr, ret);
			goto just_return;
		}
	}

	/*
	 * Check if a formatter callback returned an error that hasn't yet been
	 * returned to the controller.  Do this before the while(count>0) loop
	 * because read(2) with zero count is allowed to report errors.
	 */
	if (pdata->rsp_buf_remaining < 0) {
		BUILD_BUG_ON(ret != 0);
		ret = pdata->rsp_buf_remaining;
		pdata->rsp_buf_remaining = 0;
		goto unlock_and_return;
	}

	while (count > 0) {
		/*
		 * If a previous read response buffer has been exhausted, free
		 * it.
		 *
		 * This is done at the beginning of the while(count>0) loop
		 * because...?
		 */
		if (pdata->rsp_buf_start && !pdata->rsp_buf_remaining) {
			kfree(pdata->rsp_buf_start);
			pdata->rsp_buf_start = NULL;
			pdata->rsp_buf_pos = NULL;
		}

		/*
		 * If we have no formatter callback output queued (neither
		 * successful output nor error), go through the FIFO queue of
		 * read responses until a formatter returns non-zero (successful
		 * output or failure).
		 */
		while (pdata->rsp_buf_remaining == 0) {
			/*
			 * If pdata->rsp_invalidated is true, it means the
			 * previous read() returned an error.  Now that the
			 * error has already been propagated to userspace, we
			 * can write the end character for the invalidated read
			 * response.
			 */
			if (pdata->rsp_invalidated) {
				pdata->rsp_invalidated = false;
				goto write_end_char;
			}

			/* If we have already read some bytes successfully, even
			 * if less than requested, we should return as much as
			 * we can without blocking further.  Same if we have an
			 * error to return.
			 */
			if (non_blocking || ret != 0) {
				if (!try_wait_for_completion(
				    &pdata->read_rsp_queued)) {
					if (ret == 0)
						ret = -EAGAIN;
					/*
					 * If we are out of read responses,
					 * return whatever we have written to
					 * the userspace buffer so far, even if
					 * it's nothing.
					 */
					goto unlock_and_return;
				}
			} else {
				wait_ret = wait_for_completion_killable(
					&pdata->read_rsp_queued);
				if (wait_ret == -ERESTARTSYS) {
					if (ret == 0)
						ret = -EINTR;
					pr_warn("%s: I2C adapter %d "
						"read_rsp_queued completion "
						"wait interrupted by signal, "
						"returning %zd.\n", __func__,
						pdata->i2c_adapter.nr, ret);
					goto unlock_and_return;
				} else if (wait_ret < 0) {
					if (ret == 0)
						ret = i2c_pseudo_long_to_ssize_t
							(wait_ret);
					pr_warn("%s: I2C adapter %d "
						"read_rsp_queued completion "
						"returned unexpected error %ld,"
						" this function will return %zd"
						" now.\n", __func__,
						pdata->i2c_adapter.nr, wait_ret,
						ret);
					goto unlock_and_return;
				} else if (wait_ret > 0) {
					/* unexpected return value */
					if (ret == 0)
						ret = -ENOTRECOVERABLE;
					pr_warn("%s: I2C adapter %d "
						"read_rsp_queued completion "
						"returned unexpected value %ld,"
						" this function will return %zd"
						" now.\n", __func__,
						pdata->i2c_adapter.nr, wait_ret,
						ret);
					goto unlock_and_return;
				}
			}

			if (mutex_lock_killable(&pdata->read_rsp_queue_lock)) {
				if (ret == 0)
					ret = -EINTR;
				pr_warn("%s: I2C adapter %d "
					"read_rsp_queue_lock acquisition "
					"interrupted by signal, returning "
					"%zd.\n", __func__,
					pdata->i2c_adapter.nr, ret);
				goto unlock_and_return;
			}
			if (!list_empty(&pdata->read_rsp_queue_head))
				/* TODO: Error checking needed here? */
				rsp_wrapper = list_first_entry(
					&pdata->read_rsp_queue_head,
					struct i2c_pseudo_rsp, queue);
			/*
			 * Avoid holding pdata->read_rsp_queue_lock while
			 * executing a formatter, allocating memory, or doing
			 * anything else that might block or take non-trivial
			 * time.  This avoids blocking the enqueuing of new read
			 * responses for any significant time, even during large
			 * controller reads.
			 */
			mutex_unlock(&pdata->read_rsp_queue_lock);
			if (rsp_wrapper == NULL) {
				int_ret = i2c_pseudo_adap_is_shutdown(pdata);
				if (int_ret < 0) {
					if (ret == 0)
						ret = i2c_pseudo_int_to_ssize_t(
							int_ret);
				} else if (int_ret == 0) {
					ret = -ENOTRECOVERABLE;
					pr_err("%s: I2C adapter %d "
						"pdata->read_rsp_queue_head was"
						" empty after achieving "
						"pdata->read_rsp_queued "
						"completion, and the "
						"I2C adapter is not marked for "
						"shutdown.  This is a bug.  "
						"Returning %zd now.\n",
						__func__, pdata->i2c_adapter.nr,
						ret);
				}
				goto unlock_and_return;
			}

			pdata->rsp_buf_remaining = rsp_wrapper->formatter(
				rsp_wrapper->data, &pdata->rsp_buf_start);

			if (pdata->rsp_buf_remaining > 0) {
				pdata->rsp_buf_pos = pdata->rsp_buf_start;
				/*
				 * We consumed a completion for this rsp_wrapper
				 * but we are leaving it in
				 * pdata->read_rsp_queue_head.  Re-add a
				 * completion for it.
				 *
				 * Since overlapping reads are effectively
				 * serialized via use of pdata->rsp_lock, we
				 * could take shortcuts in how
				 * pdata->read_rsp_queued is used to avoid the
				 * need for re-incrementing it here.  However by
				 * maintaining the invariant of consuming a
				 * completion each time an item from
				 * pdata->read_rsp_queue_head is consumed
				 * (whether or not it ends up being removed from
				 * the queue in that iteration), the completion
				 * logic is simpler to follow, and more easily
				 * lends itself to a future refactor of this
				 * read operation to not hold pdata->rsp_lock
				 * continuously.
				 */
				complete(&pdata->read_rsp_queued);
				break;
			}

			/*
			 * The formatter should not mutate pdata->rsp_buf_start
			 * if it returned non-positive.  Just in case, we handle
			 * such a bug gracefully here.
			 */
			if (pdata->rsp_buf_start != NULL) {
				pr_err("%s: An I2C pseudo adapter controller "
					"read response formatter callback "
					"returned non-positive (%zd) yet "
					"mutated the response buffer address "
					"pointer (and made it non-NULL).  This "
					"is a bug in the formatter callback "
					"function.  This read response buffer "
					"will be freed without being returned "
					"to the controller.\n", __func__,
					pdata->rsp_buf_remaining);
				kfree(pdata->rsp_buf_start);
				pdata->rsp_buf_start = NULL;
			}

			mutex_lock(&pdata->read_rsp_queue_lock);
			list_del(&rsp_wrapper->queue);
			--pdata->read_rsp_queue_length;
			mutex_unlock(&pdata->read_rsp_queue_lock);

			kfree(rsp_wrapper);
			rsp_wrapper = NULL;

			/* Check if the formatter callback returned an error.
			 *
			 * If we have _not_ written any bytes to the userspace
			 * buffer yet, return now with the error code from the
			 * formatter.
			 *
			 * If we _have_ written bytes already, return now with
			 * the number of bytes written, and leave the error code
			 * from the formatter in pdata->rsp_buf_remaining so it
			 * can be returned on the next read, before any bytes
			 * are written.
			 *
			 * In either case, we deliberately return the error
			 * before writing the end character for the invalidated
			 * read response, so that the userspace controller knows
			 * to discard the response.
			 */
			if (pdata->rsp_buf_remaining < 0) {
				if (ret == 0) {
					ret = pdata->rsp_buf_remaining;
					pdata->rsp_buf_remaining = 0;
				}
				pdata->rsp_invalidated = true;
				goto unlock_and_return;
			}

 write_end_char:
			copy_size = sizeof(i2c_pseudo_ctrlr_end_char);
			/*
			 * This assertion is just in case someone changes
			 * i2c_pseudo_ctrlr_end_char to a string.  Such a change
			 * would require handling it like a read response
			 * buffer, including ensuring that we not write more
			 * than @count.  So long as it's a single character, we
			 * can avoid an extra check of @count in this code
			 * block, we already know it's greater than zero.
			 */
			BUILD_BUG_ON(copy_size != 1);
			copy_ret = copy_to_user(buf, &i2c_pseudo_ctrlr_end_char,
				copy_size);
			copy_size -= copy_ret;
			/*
			 * After writing to the userspace buffer, we need to
			 * update various counters including the return value,
			 * then continue from the start of the outer while loop
			 * because it's possible @count has reached zero.
			 *
			 * Those exact same steps must be done after copying
			 * from a read response buffer to the userspace buffer,
			 * so jump to that code instead of duplicating it.
			 */
			goto after_copy_to_user;
		}

		copy_size = max((ssize_t)0,
			min((ssize_t)count, pdata->rsp_buf_remaining));
		copy_ret = copy_to_user(buf, pdata->rsp_buf_pos, copy_size);
		copy_size -= copy_ret;
		pdata->rsp_buf_remaining -= copy_size;

		if (pdata->rsp_buf_remaining > 0) {
			pdata->rsp_buf_pos += copy_size;
		} else {
			kfree(pdata->rsp_buf_start);
			pdata->rsp_buf_start = NULL;
			pdata->rsp_buf_pos = NULL;
		}

 /*
  * When jumping here, the following variables should be set:
  *   copy_ret: Return value from copy_to_user() (bytes not copied).
  *   copy_size: The number of bytes successfully copied by copy_to_user().  In
  *       other words, this should be the size arg to copy_to_user() minus its
  *       return value (bytes not copied).
  */
 after_copy_to_user:
		ret += copy_size;
		count -= copy_size;
		buf += copy_size;

		if (copy_ret)
			goto unlock_and_return;
	}

 unlock_and_return:
	mutex_unlock(&pdata->rsp_lock);

 just_return:
	return ret;
}

/*
 * Must be called with pdata->cmd_lock held.
 *
 * Args:
 *   func_name: Set to __func__ at the call site.  Cannot be NULL.
 *   error_msg: An error message to include in the kernel log.  Should *not*
 *       have a trailing newline.  May be NULL.
 */
static void i2c_pseudo_handle_invalid_cmd(
	struct i2c_pseudo_controller *pdata, const char* func_name,
	const char *error_msg)
{
	if (error_msg != NULL)
		pr_warn("%s: %s: \"%*pE\"\n", func_name,
			error_msg, (int)pdata->cmd_size, pdata->cmd_buf);
	else
		pr_warn("%s: Invalid command: \"%*pE\"\n", func_name,
			(int)pdata->cmd_size, pdata->cmd_buf);
	/* TODO: Add an error message to pdata->read_rsp_queue_head, possibly at the head instead of tail.  Define an error message format / prefix that userspace is expected to understand.  Do *not* include func_name in the error to userspace. */
}

/* Must be called with pdata->cmd_lock held. */
/* Must never consume past first i2c_pseudo_ctrlr_end_char in @start. */
static ssize_t i2c_pseudo_receive_ctrlr_cmd_header(
	struct i2c_pseudo_controller *pdata, char *start, size_t remaining,
	bool non_blocking)
{
	bool found_deliminator_char = false;
	int i, cmd_idx;
	ssize_t copy_size, ret = 0, stop, buf_remaining;

	if (pdata->cmd_data_increment > 0) {
		/*
		 * Do not attempt to look up data bytes as a command name.
		 * Reaching here is a bug.
		 */
		ret = -ENOTRECOVERABLE;
		goto just_return_ret;
	}

	/* Never process more than we can indicate in the return value. */
	if (remaining > (size_t)SSIZE_T_MAX)
		remaining = SSIZE_T_MAX;

	buf_remaining = sizeof(pdata->cmd_buf) - pdata->cmd_size;
	stop = min((ssize_t)remaining, buf_remaining + 1);

	for (i = 0; i < stop; ++i)
		if (start[i] == i2c_pseudo_ctrlr_end_char ||
		    start[i] == i2c_pseudo_ctrlr_header_sep_char) {
			found_deliminator_char = true;
			break;
	}
	copy_size = i;

	if (copy_size > buf_remaining) {
		if (found_deliminator_char) {
			/* Reaching here is a bug. */
			ret = -ENOTRECOVERABLE;
			goto just_return_ret;
		}
		copy_size = buf_remaining;
		if (!pdata->cmd_receive_status) {
			pr_warn("%s: Error: Exceeded maximum %zu size of I2C "
				"pseudo controller command buffer for I2C "
				"adapter %d.  The command currently being "
				"written will be ignored.\n", __func__,
				sizeof(pdata->cmd_buf), pdata->i2c_adapter.nr);
			/* Positive error number is deliberate here. */
			pdata->cmd_receive_status = ENOBUFS;
		}
	}

	memcpy(&pdata->cmd_buf[pdata->cmd_size], start, copy_size);
	pdata->cmd_size += copy_size;

	if (!found_deliminator_char || pdata->cmd_size <= 0)
		goto no_deliminator_yet;

	/* This may be negative. */
	cmd_idx = pdata->cmd_idx_plus_one - 1;

	if (cmd_idx < 0) {
		for (i = 0; i < ARRAYLEN(i2c_pseudo_cmds); ++i)
			if (i2c_pseudo_cmds[i].cmd_size == pdata->cmd_size &&
			    !memcmp(i2c_pseudo_cmds[i].cmd_string,
				    pdata->cmd_buf, pdata->cmd_size))
				break;
		if (i >= ARRAYLEN(i2c_pseudo_cmds)) {
			ret = -EINVAL;
			pr_debug("%s: unrecognized command \"%*pE\"\n",
				__func__, (int)pdata->cmd_size, pdata->cmd_buf);
			/* TODO: File an error read response. */
			i2c_pseudo_handle_invalid_cmd(pdata, __func__,
				"unrecognized command");
			goto clear_buffer_and_return;
		}
		cmd_idx = i;
		pdata->cmd_idx_plus_one = cmd_idx + 1;
	}

	/*
	 * If we have write bytes queued and we encountered
	 * i2c_pseudo_ctrlr_end_char or i2c_pseudo_ctrlr_header_sep_char, invoke
	 * the header_receiver callback.
	 */
	if (!pdata->cmd_receive_status &&
	    i2c_pseudo_cmds[cmd_idx].header_receiver != NULL) {
		ret = i2c_pseudo_cmds[cmd_idx].header_receiver(
			pdata->cmd_data[cmd_idx], pdata->cmd_buf,
			pdata->cmd_size, non_blocking);
		if (ret > 0) {
			if (ret > sizeof(pdata->cmd_buf)) {
				pr_err("%s: A header_receiver callback "
					"returned a data buffer "
					"increment size of %zd, which "
					"is greater than the maximum "
					"controller write reply buffer "
					"size of %zu.  This is a bug.  "
					"Will return -ENOTRECOVERABLE."
					"\n", __func__, ret,
					sizeof(pdata->cmd_buf));
				ret = -ENOTRECOVERABLE;
				goto clear_buffer_and_return;
			}
			pdata->cmd_data_increment = ret;
		} else if (ret < 0) {
			pdata->cmd_receive_status =
				i2c_pseudo_ssize_t_to_int(ret);
		}
	}

 clear_buffer_and_return:
	pdata->cmd_size = 0;
	/* This is for safety in the face of bugs, not correctness. */
	memset(pdata->cmd_buf, 0, sizeof(pdata->cmd_buf));

 no_deliminator_yet:
	if (ret >= 0)
		return copy_size + found_deliminator_char;

 just_return_ret:
	if (ret < 0 && pdata->cmd_idx_plus_one >= 1 &&
	    !pdata->cmd_receive_status)
		pdata->cmd_receive_status = -i2c_pseudo_ssize_t_to_int(ret);
	return ret;
}

/* Must be called with pdata->cmd_lock held. */
/* Must never consume past first i2c_pseudo_ctrlr_end_char in @start. */
static ssize_t i2c_pseudo_receive_ctrlr_cmd_data(
	struct i2c_pseudo_controller *pdata, char *start, size_t remaining,
	bool non_blocking)
{
	ssize_t i, ret, size_holder;
	int cmd_idx;

	/* This may be negative. */
	cmd_idx = pdata->cmd_idx_plus_one - 1;

	if (cmd_idx < 0) {
		pr_err("%s: Attempting to process write bytes as data without a"
			" write command previously identified.  This is a bug."
			"  Will return -ENOTRECOVERABLE.\n", __func__);
		return -ENOTRECOVERABLE;
	}

	/* Never process more than we can indicate in the return value. */
	if (remaining > (size_t)SSIZE_T_MAX)
		remaining = SSIZE_T_MAX;

	size_holder = min(
		(sizeof(pdata->cmd_buf) -
		 (sizeof(pdata->cmd_buf) % pdata->cmd_data_increment)) -
		pdata->cmd_size,
		(((pdata->cmd_size + remaining) /
		  pdata->cmd_data_increment) *
		 pdata->cmd_data_increment) - pdata->cmd_size);

	/* Size of current buffer plus all remaining write bytes. */
	size_holder = pdata->cmd_size + remaining;
	/*
	 * Avoid rounding down to zero.  If there are insufficient write
	 * bytes remaining to grow the buffer to 1x of the requested
	 * data byte increment, we'll copy what is available to the
	 * buffer, and just leave it queued without any further command
	 * handler invocations in this write() (unless
	 * i2c_pseudo_ctrlr_end_char is found, in which case we will
	 * always invoke the data_receiver for any remaining data bytes,
	 * and will always invoke the cmd_completer).
	 */
	if (size_holder > pdata->cmd_data_increment)
		/*
		 * Round down to the nearest multiple of the requested
		 * data byte increment.
		 */
		size_holder -= size_holder % pdata->cmd_data_increment;
	/*
	 * Take the smaller of:
	 *
	 * [A] 1st min() arg: The number of bytes that we would want the
	 * buffer to end up with if it had unlimited space (computed
	 * above).
	 *
	 * [B] 2nd min() arg: The number of bytes that we would want the
	 * buffer to end up with if there were unlimited write bytes
	 * remaining (computed in-line below).
	 */
	size_holder = min(size_holder, (ssize_t)(sizeof(pdata->cmd_buf) - (
		sizeof(pdata->cmd_buf) % pdata->cmd_data_increment)));
	/*
	 * Subtract the existing buffer size to get the number of bytes
	 * we actually want to copy from the remaining write bytes in
	 * this loop iteration, assuming no i2c_pseudo_ctrlr_end_char.
	 */
	size_holder -= pdata->cmd_size;

	/*
	 * Look for i2c_pseudo_ctrlr_end_char.  If we find it, we will
	 * copy up to but *not* including its position.
	 */
	for (i = 0; i < size_holder; ++i)
		if (start[i] == i2c_pseudo_ctrlr_end_char)
			break;

	/* Copy from the remaining write bytes to the command buffer. */
	memcpy(&pdata->cmd_buf[pdata->cmd_size], start, i);
	pdata->cmd_size += i;

	/*
	 * If we have write bytes queued and *either* we encountered
	 * i2c_pseudo_ctrlr_end_char *or* we have a multiple of
	 * pdata->cmd_data_increment, invoke the data_receiver callback.
	 */
	if (pdata->cmd_size > 0 &&
	    (i < size_holder ||
	     pdata->cmd_size % pdata->cmd_data_increment == 0)) {
		if (!pdata->cmd_receive_status) {
			ret = i2c_pseudo_cmds[cmd_idx].data_receiver(
				pdata->cmd_data[cmd_idx], pdata->cmd_buf,
				pdata->cmd_size, non_blocking);
			if (ret < 0)
				pdata->cmd_receive_status =
					i2c_pseudo_ssize_t_to_int(ret);
		}
		pdata->cmd_size = 0;
		/* This is for safety in the face of bugs, not correctness. */
		memset(pdata->cmd_buf, 0, sizeof(pdata->cmd_buf));
	}

	/* If i2c_pseudo_ctrlr_end_char was found, skip past it. */
	if (i < size_holder)
		++i;
	return i;
}

/* Must be called with pdata->cmd_lock held. */
static int i2c_pseudo_receive_ctrlr_cmd_complete(
	struct i2c_pseudo_controller *pdata, bool non_blocking)
{
	int ret = 0, cmd_idx;

	/* This may be negative. */
	cmd_idx = pdata->cmd_idx_plus_one - 1;

	if (cmd_idx >= 0 && i2c_pseudo_cmds[cmd_idx].cmd_completer != NULL) {
		ret = i2c_pseudo_cmds[cmd_idx].cmd_completer(
			pdata->cmd_data[cmd_idx], pdata->cmd_receive_status,
			non_blocking);
		switch (ret) {
		case I2C_PSEUDO_CMD_COMPLETER_SUCCESS:
			ret = 0;
			break;
		case I2C_PSEUDO_CMD_COMPLETER_SHUTDOWN:
			ret = i2c_pseudo_adap_set_shutdown(pdata);
			if (ret > 0)
				ret = 0;
			break;
		default:
			if (ret >= 0)
				/* Invalid return value from cmd_completer. */
				ret = -ENOTRECOVERABLE;
		}
	}

	pdata->cmd_idx_plus_one = 0;
	pdata->cmd_receive_status = 0;
	pdata->cmd_data_increment = 0;

	pdata->cmd_size = 0;
	/* This is for safety in the face of bugs, not correctness. */
	memset(pdata->cmd_buf, 0, sizeof(pdata->cmd_buf));

	return ret;
}

static ssize_t i2c_pseudo_cdev_write(struct file *filep, const char __user *buf,
		size_t count, loff_t *f_ps)
{
	ssize_t ret = 0;
	bool non_blocking;
	size_t remaining;
	char *kbuf, *start;
	struct i2c_pseudo_controller *pdata;

	/*
	 * Just in case this could change out from under us, best to keep a
	 * consistent view for the duration of this syscall.
	 *
	 * Write command implementations, i.e.
	 * struct i2c_pseudo_cmd implementations, do NOT have to support
	 * blocking writes.  For example, if a write of an I2C message reply is
	 * received for a message that the pseudo adapter never requested or
	 * expected, it makes more sense to indicate an error than to block
	 * until possibly receiving a master_xfer request for that I2C message,
	 * even if blocking is permitted.
	 *
	 * Furthermore, controller writes MUST NEVER block indefinitely, even
	 * when non_blocking is false.  E.g. while non_blocking may be used to
	 * select between mutex_trylock and mutex_lock_killable, even in the
	 * latter case the lock should never be blocked on I/O, on userspace, or
	 * on anything else outside the control of this driver.  It IS
	 * permissable for the lock to be blocked on processing of previous or
	 * concurrent write input, so long as that processing does not violate
	 * these rules.
	 */
	non_blocking = !!(filep->f_flags & O_NONBLOCK);
	pdata = filep->private_data;

	/* Never process more than we can indicate in the return value. */
	if (count > (size_t)SSIZE_T_MAX)
		count = SSIZE_T_MAX;

	kbuf = kzalloc(count, GFP_KERNEL);
	if (kbuf == NULL) {
		pr_err("%s: kzalloc(%zu, GFP_KERNEL) returned NULL\n", __func__,
			count);
		ret = -ENOMEM;
		goto free_and_return;
	}
	if (copy_from_user(kbuf, buf, count)) {
		pr_err("%s: copy_from_user() failed to copy entire buffer\n",
			__func__);
		goto free_and_return;
	}

	start = kbuf;
	remaining = count;

	/*
	 * Since write() calls are effectively serialized by way of
	 * pdata->cmd_lock, we MUST NOT block on obtaining that lock if in
	 * non-blocking mode, because it might be held by a blocking write().
	 */
	if (non_blocking) {
		if (!mutex_trylock(&pdata->cmd_lock)) {
			BUILD_BUG_ON(ret != 0);
			ret = -EAGAIN;
			pr_debug("%s: Could not immediately acquire I2C "
				"adapter %d cmd_lock for O_NONBLOCK write, "
				"returning %zd.\n", __func__,
				pdata->i2c_adapter.nr, ret);
			goto free_and_return;
		}
	} else {
		if (mutex_lock_killable(&pdata->cmd_lock)) {
			BUILD_BUG_ON(ret != 0);
			ret = -EINTR;
			pr_warn("%s: I2C adapter %d cmd_lock acquisition "
				"interrupted by signal, returning %zd.\n",
				__func__, pdata->i2c_adapter.nr, ret);
			goto free_and_return;
		}
	}

	while (remaining) {
		if (pdata->cmd_data_increment <= 0)
			ret = i2c_pseudo_receive_ctrlr_cmd_header(
				pdata, start, remaining, non_blocking);
		else
			ret = i2c_pseudo_receive_ctrlr_cmd_data(
				pdata, start, remaining, non_blocking);
		if (ret < 0)
			break;
		if (ret == 0 || ret > remaining) {
			pr_err("%s: Received %zd return value from either "
				"i2c_pseudo_receive_ctrlr_cmd_header() or "
				"i2c_pseudo_receive_ctrlr_cmd_data(), which is "
				"either zero or greater than the %zu remaining "
				"write bytes.  Either case is a bug.  Will "
				"return -ENOTRECOVERABLE.\n", __func__, ret,
				remaining);
			ret = -ENOTRECOVERABLE;
			break;
		}

		remaining -= ret;
		start += ret;

		if (ret > 0 && start[-1] == i2c_pseudo_ctrlr_end_char) {
			ret = i2c_pseudo_int_to_ssize_t(
				i2c_pseudo_receive_ctrlr_cmd_complete(
					pdata, non_blocking));
			if (ret < 0)
				break;
		}
	}

	mutex_unlock(&pdata->cmd_lock);

	if (ret >= 0)
		/* If successful the whole write is always consumed. */
		ret = count;

 free_and_return:
	kfree(kbuf);
	return ret;
}

/*
 * The select/poll/epoll implementation in this module is designed around these
 * controller behavior assumptions:
 *
 * - If any reader of a given controller makes use of polling, all will.
 *
 * - Upon notification of available data to read, a reader will fully consume it
 *   in a read() loop until receiving EAGAIN or EWOULDBLOCK.
 *
 * - Only one reader need be woken upon newly available data, however it is okay
 *   if more than one are sometimes woken.
 *
 * - If more than one reader is woken, or otherwise acts in parallel, it is the
 *   responsibility of the readers to either ensure that only one at a time
 *   consumes all input until EAGAIN/EWOULDBLOCK, or that they properly
 *   recombine any data that was split among them.
 *
 * - All of the above applies to writers as well.
 *
 * Notes:
 *
 * - If a reader does not read all available data until EAGAIN/EWOULDBLOCK after
 *   being woken from poll, there may be no wake event for the remaining
 *   available data, causing it to remain unread until further data becomes
 *   available and triggers another wake event.  The same applies to writers -
 *   they are only guaranteed to be woken /once/ per blocked->unblocked
 *   transition, so after being woken they should continue writing until either
 *   the controller is out of data or EAGAIN/EWOULDBLOCK is encountered.
 *
 * - It is strongly suggested that controller implementations have only one
 *   reader (thread) and one writer (thread), which may or may not be the same
 *   thread.  After all only one message can be active on an I2C bus at a time,
 *   and this driver implementation reflects that.  Avoiding multiple readers
 *   and multiple writers greatly simplifies controller implementation, and
 *   there is likely nothing to be gained from performing any of their work in
 *   parallel.
 *
 * - Implementation detail: Reads are effectively serialized by a per controller
 *   read lock.  From the perspective of other readers, the controller device
 *   will appear blocked, with appropriate behavior based on the O_NONBLOCK bit.
 *   THIS IS SUBJECT TO CHANGE!
 *
 * - Implementation detail: Writes are effectively serialized by a per
 *   controller write lock.  From the perspective of other writers, the
 *   controller device will appear blocked, with appropriate behavior based on
 *   the O_NONBLOCK bit.  THIS IS SUBJECT TO CHANGE!
 *
 * - Implementation detail: In the initial implementation, the only scenario
 *   where a controller will appear blocked for writes is if another write is in
 *   progress.  Thus, a single writer should never see the device blocked.  THIS
 *   IS SUBJECT TO CHANGE!  When using O_NONBLOCK, a controller should correctly
 *   handle EAGAIN/EWOULDBLOCK even if it has only one writer.
 */
static __poll_t i2c_pseudo_cdev_poll(struct file *filep, poll_table *ptp)
{
	__poll_t poll_ret = 0;
	int int_ret;
	struct i2c_pseudo_controller *pdata;

	pdata = filep->private_data;

	/* TODO: Any reason for separate read and write wait queues? */
	poll_wait(filep, &pdata->poll_wait_queue, ptp);

	if (mutex_trylock(&pdata->rsp_lock)) {
		if (i2c_pseudo_poll_in(pdata))
			poll_ret |= POLLIN | POLLRDNORM;
		mutex_unlock(&pdata->rsp_lock);
	}

	if (!mutex_is_locked(&pdata->cmd_lock))
		poll_ret |= POLLOUT | POLLWRNORM;

	int_ret = i2c_pseudo_adap_is_shutdown(pdata);
	if (int_ret < 0)
		/*
		 * POLLERR indicates an error condition occurred on the
		 * file descriptor.  Strictly speaking that is not what an error
		 * from i2c_pseudo_adap_is_shutdown() indicates.  POLLERR is
		 * used here to punt the decision of whether and when to retry
		 * polling to the userspace process, without falsely claiming
		 * that the file descriptor is readable, writable, or will
		 * return EOF.
		 *
		 * At the time of this writing, i2c_pseudo_adap_is_shutdown()
		 * can only return negative (error) upon killable signal, so
		 * what we return here should be irrelevant.  However that is
		 * subject to change!
		 */
		poll_ret |= POLLERR;
	else if (int_ret > 0)
		poll_ret |= POLLHUP;

	return poll_ret;
}

static const struct file_operations i2c_pseudo_fileops = {
	.owner = THIS_MODULE,
	.open = i2c_pseudo_cdev_open,
	.release = i2c_pseudo_cdev_release,
	.read = i2c_pseudo_cdev_read,
	.write = i2c_pseudo_cdev_write,
	.poll = i2c_pseudo_cdev_poll,
	.llseek = no_llseek,
};

static void i2c_p_device_release(struct device *dev)
{
	/*
	 * i2c_pseudo_device is statically allocated, however the
	 * struct device.release field must not be NULL, so we set it to this
	 * do-nothing function.
	 */
}

inline static void i2c_p_device_delete(void)
{
	device_del(&i2c_pseudo_device);
}

inline static void i2c_p_device_put(void)
{
	put_device(&i2c_pseudo_device);
	memset(&i2c_pseudo_device, 0, sizeof(i2c_pseudo_cdev));
}

inline static void i2c_p_cdev_delete(void)
{
	cdev_del(&i2c_pseudo_cdev);
}

inline static void i2c_p_cdev_put(bool cdev_del_called)
{
	/*
	 * Set cdev_del_called to false when cdev_del(&i2c_pseudo_cdev) has been
	 * called.  That function calls kobject_put(&cdev->kobj), and the other
	 * function it calls is not exported (as of Linux 4.18.4), so this
	 * condition is necessary to avoid a duplicate
	 * kobject_put(&i2c_pseudo_cdev.kobj) call.
	 *
	 * This calls kobject_put(cdev.kobj) directly instead of calling
	 * cdev_put() because (as of Linux 4.18.4) the latter also calls
	 * module_put(cdev.owner).  This module does not make a corresponding
	 * increment to THIS_MODULE reference count because there appears to be
	 * no precedent for that in other kernel drivers, and because cdev_del()
	 * does _not_ do module_put(cdev.owner), so if THIS_MODULE's reference
	 * count were self-incremented, then we would need to special case
	 * decrementing it in the usual code path of using cdev_del().
	 */
	if (!cdev_del_called)
		kobject_put(&i2c_pseudo_cdev.kobj);
	memset(&i2c_pseudo_cdev, 0, sizeof(i2c_pseudo_cdev));
}

/* Requires &i2c_pseudo_counters.lock to be initialized. */
static void i2c_p_all_controllers_free(void)
{
	struct i2c_pseudo_controller **all_controllers;

	mutex_lock(&i2c_pseudo_counters.lock);
	all_controllers = i2c_pseudo_counters.all_controllers;
	i2c_pseudo_counters.all_controllers = NULL;
	mutex_unlock(&i2c_pseudo_counters.lock);

	/* TODO: Is there a race condition between this and the i2c_pseudo_remove_from_counters() invocations?  E.g. if root were to rmmod while there is no I2C dapter (during either i2c_pseudo_cdev_open() or i2c_pseudo_cdev_release(), such that no I2C adapter or device has a reference count to THIS_MODULE.  If there is a race, can we resolve it with THIS_MODULE reference count increments+decrements corresponding to all_controllers entries being added or removed? */
	kfree(all_controllers);
}

inline static void i2c_p_chrdev_deregister(void)
{
	unregister_chrdev_region(i2c_pseudo_dev_num, I2C_PSEUDO_CDEV_COUNT);
}

inline static void i2c_p_class_destroy(void)
{
	struct class *class;
	class = i2c_pseudo_class;
	i2c_pseudo_class = NULL;
	class_destroy(class);
}

static int __init i2c_pseudo_init(void)
{
	int ret = 0;
	bool cdev_del_called = false;

	/* Cap the I2C pseudo adapters limit to something sane. */
	if (i2c_pseudo_limit < I2C_PSEUDO_ADAPTERS_MIN ||
	    i2c_pseudo_limit > I2C_PSEUDO_ADAPTERS_MAX) {
		pr_err("%s: i2c_pseudo_limit=%u, must be in range ["
			STR(I2C_PSEUDO_ADAPTERS_MIN) ", "
			STR(I2C_PSEUDO_ADAPTERS_MAX) "]\n", __func__,
			i2c_pseudo_limit);
		ret = -EINVAL;
		goto fail_beginning;
	}

	mutex_init(&i2c_pseudo_counters.lock);
	/* TODO: Is there any reason to hold the lock here? */
	i2c_pseudo_counters.all_controllers = kcalloc(i2c_pseudo_limit,
		sizeof(*i2c_pseudo_counters.all_controllers), GFP_KERNEL);
	if (i2c_pseudo_counters.all_controllers == NULL) {
		pr_err("%s: kcalloc(%u, %zu, GFP_KERNEL) returned NULL\n",
			__func__, i2c_pseudo_limit,
			sizeof(*i2c_pseudo_counters.all_controllers));
		goto fail_beginning;
	}

	i2c_pseudo_class = class_create(THIS_MODULE, I2C_PSEUDO_CLASS_NAME);
	if (IS_ERR(i2c_pseudo_class)) {
		ret = PTR_ERR(i2c_pseudo_class);
		pr_err("%s: class_create(THIS_MODULE, "
			STR(I2C_PSEUDO_CLASS_NAME) ") failed with error %d\n",
			__func__, -ret);
		goto fail_after_all_controllers_alloc;
	}

	/* Register character device numbers. */
	ret = alloc_chrdev_region(
		&i2c_pseudo_dev_num, I2C_PSEUDO_CDEV_BASEMINOR,
		I2C_PSEUDO_CDEV_COUNT, I2C_PSEUDO_CHRDEV_NAME);
	if (ret < 0) {
		pr_err("%s: alloc_chrdev_region(&dev, "
			STR(I2C_PSEUDO_CDEV_BASEMINOR) ", "
			STR(I2C_PSEUDO_CDEV_COUNT) ", "
			STR(I2C_PSEUDO_CHRDEV_NAME) ") failed with error %d\n",
			__func__, -ret);
		goto fail_after_class_create;
	}

	/* Initialize the character device. */
	cdev_init(&i2c_pseudo_cdev, &i2c_pseudo_fileops);
	i2c_pseudo_cdev.owner = THIS_MODULE;
	ret = kobject_set_name(&i2c_pseudo_cdev.kobj, "%s",
		I2C_PSEUDO_CDEV_NAME);
	if (ret < 0) {
		pr_err("%s: kobject_set_name(&cdev.kobj, \"%%s\", "
			STR(I2C_PSEUDO_CDEV_NAME) ") failed with error %d\n",
			__func__, -ret);
		goto fail_after_chrdev_register;
	}

	/* Add the character device. */
	ret = cdev_add(&i2c_pseudo_cdev, i2c_pseudo_dev_num,
		I2C_PSEUDO_CDEV_COUNT);
	if (ret < 0) {
		pr_err("%s: cdev_add(&cdev, %u, " STR(I2C_PSEUDO_CDEV_COUNT)
			") failed with error %d\n", __func__,
			i2c_pseudo_dev_num, -ret);
		goto fail_after_cdev_init;
	}

	/* Initialize the controller device. */
	device_initialize(&i2c_pseudo_device);
	i2c_pseudo_device.devt = i2c_pseudo_cdev.dev;
	i2c_pseudo_device.class = i2c_pseudo_class;
	i2c_pseudo_device.release = i2c_p_device_release;
	ret = dev_set_name(&i2c_pseudo_device, "%s", I2C_PSEUDO_DEVICE_NAME);
	if (ret < 0) {
		pr_err("%s: dev_set_name(&device, \"%%s\", "
			STR(I2C_PSEUDO_DEVICE_NAME) ") failed with error %d\n",
			__func__, -ret);
		goto fail_after_device_init;
	}

	/* Add the controller device. */
	ret = device_add(&i2c_pseudo_device);
	if (ret < 0) {
		pr_err("%s: device_add(&device) failed with error %d\n",
			__func__, -ret);
		goto fail_after_device_init;
	}

	/* Return success. */
	ret = 0;
	goto just_return;

 fail_after_device_init:
	i2c_p_device_put();
	i2c_p_cdev_delete();
	cdev_del_called = true;
 fail_after_cdev_init:
	i2c_p_cdev_put(cdev_del_called);
 fail_after_chrdev_register:
	i2c_p_chrdev_deregister();
 fail_after_class_create:
	i2c_p_class_destroy();
 fail_after_all_controllers_alloc:
	i2c_p_all_controllers_free();
 fail_beginning:
	if (ret >= 0) {
		pr_err("%s: Jumped to failure cleanup section "
			"with non-negative return value %d set.  This is a "
			"bug.  Will return -1 instead.\n", __func__, ret);
		ret = -1;
	}
 just_return:
	return ret;
}

/*
 * This function is for compile-time assertions that do not otherwise belong in
 * a single specific function.
 */
inline static void i2c_pseudo_build_assertions(void)
{
	BUILD_BUG_ON(STRLEN("") != 0);
	BUILD_BUG_ON(STRLEN("a") != 1);
	BUILD_BUG_ON(STRLEN("abc") != 3);
	BUILD_BUG_ON(ARRAYLEN(i2c_pseudo_cmds) <= 0);
	BUILD_BUG_ON(ARRAYLEN(i2c_pseudo_cmds) > I2C_PSEUDO_CMDS_SANITY_LIMIT);
	BUILD_BUG_ON(ARRAYLEN(i2c_pseudo_cmds) != I2C_PSEUDO_NUM_WRITE_CMDS);
	/*
	 * If this starts failing because you've changed cmd_buf to be allocated
	 * separately from struct i2c_pseudo_controller, be sure to audit its
	 * uses for those which make this same assumption.
	 */
	BUILD_BUG_ON(sizeof(((struct i2c_pseudo_controller *)0)->cmd_buf) !=
		I2C_PSEUDO_CTRLR_CMD_BUF_SIZE);
}

static void __exit i2c_pseudo_exit(void)
{
	i2c_p_device_delete();
	i2c_p_device_put();
	i2c_p_cdev_delete();
	i2c_p_cdev_put(true);
	i2c_p_chrdev_deregister();
	i2c_p_class_destroy();
	i2c_p_all_controllers_free();

	i2c_pseudo_build_assertions();
}

MODULE_AUTHOR("Matthew Blecker <matthewb@ihavethememo.net");
MODULE_DESCRIPTION("Driver for userspace I2C adapter implementations.");
MODULE_LICENSE("GPL");

module_init(i2c_pseudo_init);
module_exit(i2c_pseudo_exit);
