#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

	// Function declarations
	void register_system_common(void);
    void register_nvs(void);
    void register_servo(void);
    void register_servo_key(void);
    void register_servo_calib(void);
    void register_motor_set(void);
    void register_motor_stop(void);
    void register_dfplayer_play_folder(void);

#ifdef __cplusplus
}
#endif

