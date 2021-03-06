/*
 * payload_mgr.h
 *
 *  Created on: Nov 5, 2010
 *      Author: tgorski
 */

#ifndef PAYLOAD_MGR_H_
#define PAYLOAD_MGR_H_

#include "pins.h"
#include "gpio.h"
#include "sensor_svc.h"

#define set_backend_power_pin(v)      {if (!v) gpio_clr_gpio_pin(UC_PWRENOUT); else gpio_set_gpio_pin(UC_PWRENOUT); }
#define get_backend_power_pin         (gpio_get_pin_value(UC_PWRENOUT))

#define FRU_CTLCODE_COLD_RST          (0)       // FRU Control command cold reset code
#define FRU_CTLCODE_WARM_RST          (1)       // FRU Control command warm reset code
#define FRU_CTLCODE_REBOOT            (2)       // FRU Control command reboot code
#define FRU_CTLCODE_QUIESCE           (4)       // FRU Control command quiesce code

#define MAX_PWR_ENA_PIN_CNT           (6)       // maximum number of power enable pins
#define MAX_PWR_ENA_STAGE_CNT         (4)       // maximum number of power enable sequential stages
#define PWR_ENA_PIN_UNDEFINED         (0xff)    // logical power enable pin value used when the physical pin does not exist
#define PWR_ENA_MIN_PIN_OFFSET        (0)       // minimum bit offset for power enable pin
#define PWR_ENA_MAX_PIN_OFFSET        (MAX_PWR_ENA_PIN_CNT-1)     // maximum bit offset for power enable pin

#define PWRCOMP_PAYLD_12V             (0)       // comparator for main 12V
#define PWRCOMP_AUX_12V               (1)       // comparator for aux 12V

typedef enum {status_OK=0, noncritical, critical, fault} alarm_level_t;
typedef enum {none=0, temperature, voltage, backend_device} alarm_source_class_t;
//typedef enum {mode_0, mode_1, mode_2, mode_3, mode_off} boot_mode_setting_t;
typedef enum {power_off=0, power_on} power_status_t;

typedef struct {
	unsigned int cur_on_state         : 1;
	unsigned int prev_on_state        : 1;
	unsigned int unused               : 6;
  unsigned char upper_thr;                  // upper hysteresis threshold
  unsigned char lower_thr;                  // lower hysteresis threshold
} voltage_comparator_rec_t;


typedef unsigned char physical_power_ena_pin_map_t[MAX_PWR_ENA_PIN_CNT];

typedef struct {
	unsigned char enamask[MAX_PWR_ENA_STAGE_CNT];
  unsigned char stagedelay_ms[MAX_PWR_ENA_STAGE_CNT-1];
  unsigned char defined_stage_cnt;
} power_on_mask_config_t;


typedef struct {
  alarm_level_t cur_alarm_level;            // current alarm level
  alarm_source_class_t cur_alarm_source;    // current alarm source
} backend_alarm_rec_t;

// trim codes for MGT regulators
#define MGTTRIM_0pct                    (0)         // 0% trim
#define MGTTRIM_m5pct                   (1)         // -5% trim
#define MGTTRIM_m3pct                   (2)         // -3% trim
#define MGTTRIM_m1pct                   (3)         // -1% trim
#define MGTTRIM_p1pct                   (4)         // +1% trim
#define MGTTRIM_p3pct                   (5)         // +3% trim
#define MGTTRIM_p5pct                   (6)         // +5% trim

// setting codes for MGT regulators
#define MGTVSET_low                     (0)
#define MGTVSET_high                    (1)

// LDO regulator IDs
#define MGTLDO_1p0VL                    (0)
#define MGTLDO_1p0VR                    (1)
#define MGTLDO_1p2VL                    (2)
#define MGTLDO_1p2VR                    (3)

typedef struct {
  union {
    unsigned short uint16;
    struct {
      unsigned left1p0_set : 1;             // left (frontpanel) side 1.0V setpoint -- 0-1.0V, 1-1.05V
      unsigned right1p0_set : 1;            // right (backplane) side 1.0V setpoint -- 0-1.0V, 1-1.05V
      unsigned left1p0_margin : 3;          // margin code for left side 1.0V
      unsigned left1p2_margin : 3;          // margin code for left side 1.2V
      unsigned right1p0_margin : 3;         // margin code for right side 1.0V
      unsigned right1p2_margin : 3;         // margin code for right side 1.2V
      unsigned unused : 2;                  // unused bits
    } bits;
  };
} MGT_Vreg_settings_t;

/*
// codes for MGT regulator bits
#define MGTLDO_1p0L_MSEL              (0)
#define MGTLDO_1p0L_MTOL              (1)
#define MGTLDO_1p0L_VSEL              (2)
#define MGTLDO_1p0R_MSEL              (3)
#define MGTLDO_1p0R_MTOL              (4)
#define MGTLDO_1p0R_VSEL              (5)
#define MGTLDO_1p2L_MSEL              (6)
#define MGTLDO_1p2L_MTOL              (7)
#define MGTLDO_1p2R_MSEL              (8)
#define MGTLDO_1p2R_MTOL              (9)
*/
typedef struct {
  unsigned int pin;                                 // GPIO pin index
  unsigned int bit0;                                // GPLP bit 0 index;
  unsigned int bit1;                                // GPLP bit 1 index
} MGT_ldo_control_point_t;                        

typedef struct {
  const MGT_ldo_control_point_t* MSEL_cp;           // MSEL pin
  const MGT_ldo_control_point_t* MTOL_cp;           // MTOL pin
  const MGT_ldo_control_point_t* VSET_cp;           // VSET pin (used on 1.0V regulators only)
} MGT_ldo_control_set_t;


typedef struct {
	unsigned short cold_reset_timer;                  // countdown timer used during cold resets
	unsigned short quiesce_timer1;                    // initial holdoff timer for quiesce (minimum delay)
  unsigned short quiesce_timer2;                    // second stage timer for quiesce (max remaining delay to forced quiesced state)
	unsigned short payload_startup_timer;             // timer for holding off sensors during startup of payload power
	unsigned short backend_startup_timer;             // timer for holding off sensors & sequencing staged enables during startup of backend power
  unsigned short sec_util_timer;                   // timer that sends event every 1 second for utility purposes
	unsigned char backend_cur_stage;                  // stage index for back end power up -- set to 0xff when power-up complete
	unsigned long backend_cur_timer;                  // timer used after pin change in backend power sequence
	unsigned long backend_cur_mask;                   // current power enable (port B mapped) mask in effect
	unsigned long pwrena_mask[MAX_PWR_ENA_STAGE_CNT]; // pin enable masks mapped to IO pins (port B) for fast masking
	unsigned short warm_reset_timer;                  // timer for producing the reset pulse
	unsigned short fpga_load_timer;                   // timer for producing the FPGA program strobe
	unsigned short autoconfig_poll_timer;             // timer for regulating the autoconfig poll interval
	//unsigned long mmc_uptime;                         // up time of the MMC in seconds
	//unsigned long bkend_hottime;                      // hot time for the back end in seconds
} state_record_t;


typedef struct {
	unsigned int bkend_pwr_ena        : 1;            // back end power on/off enable
	unsigned int autocfg_ena          : 1;            // autoconfig enable
	unsigned int bkend_shtdn_evt_ena  : 1;            // send hotswap event on backend shutdown
	unsigned int aux_12V_pwr_select   : 1;            // 1=select aux 12V power, 0=do not select aux 12V power
	alarm_level_t global_mask_level   : 3;            // global mask level, only sensor thresholds exceeding this state will be processed--method of masking off non-critical and critical signals from payload mgr
	unsigned int unused1              : 1;
	power_on_mask_config_t bkend_pwr_cfg;               // configuration of backend power-on sequence
	unsigned char sensor_mask_level[MAX_SENSOR_CNT];    // mask level for each sensor
  MGT_Vreg_settings_t MGT_Vreg;
}  nonvolatile_settings_t;


typedef struct {
  state_record_t ctl;                           // state variables used to manage functions
  voltage_comparator_rec_t payld_pwr[2];        // comparators for main and auxilliary payload 12V 
  backend_alarm_rec_t alarm;                    // alarm record
  nonvolatile_settings_t settings;              // power settings
  power_status_t payld_pwr_cur_state;           // current state of payload power
} payload_mgr_state_t;


extern payload_mgr_state_t pyldmgr_state;

//extern payload_mgr_eeprom_rec_t pyldmgr_eeprom_settings;

extern void pyldmgr_init(void);

extern void pyldmgr_service(void);

extern void pyldmgr_ipmicmd_fru_ctrl(const unsigned char ctlcode);

extern void pyldmgr_ipmicmd_backend_pwr(const ipmb_msg_desc_t* prqmsg);

extern __inline__ power_status_t pyldmgr_get_payload_power_status(void);

extern power_status_t pyldmgr_get_backend_power_pin_status(void);

extern power_status_t pyldmgr_get_backend_power_status(void);

extern void apply_trim_settings_to_pins(const unsigned int margcode, const unsigned int LDO_ID);

extern void apply_trim_settings_to_reg(const unsigned int margcode, const unsigned int LDO_ID, MGT_Vreg_settings_t*const pvrs);

extern void apply_voltage_settings_to_pins(const unsigned int vset, const unsigned int LDO_ID);

extern void apply_voltage_settings_to_reg(const unsigned int vset, const unsigned int LDO_ID, MGT_Vreg_settings_t*const pvrs);

#endif /* PAYLOAD_MGR_H_ */
