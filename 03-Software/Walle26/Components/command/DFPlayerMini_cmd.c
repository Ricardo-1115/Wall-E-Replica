#include "esp_console.h"
#include "argtable3/argtable3.h"  
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DFPlayerMini.h"

static const char *TAG = "DFPlayerMini_cmd";
static struct {
    struct arg_int *folder;
    struct arg_int *file;
    struct arg_int *volume;
    struct arg_end *end;
} play_folder_args_t;

static int play_folder_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &play_folder_args_t);
    if (nerrors != 0) {
        arg_print_errors(stderr, play_folder_args_t.end, argv[0]);
        return 1;
    }

    int folder = play_folder_args_t.folder->ival[0];
    int file = play_folder_args_t.file->ival[0];
    int volume = play_folder_args_t.volume->count > 0 ? play_folder_args_t.volume->ival[0] : 9; // 默认音量为9
    if (folder < 1 || folder > 2) {
        ESP_LOGE(TAG, "Invalid folder number: %d. Must be between 1 and 2.\n", folder);
        return 1;
    }
    if (file < 1 || file > 27) {
        ESP_LOGE(TAG, "Invalid file number: %d. Must be between 1 and 27.\n", file);
        return 1;
    }
    if(volume < 0 || volume > 30) {
        ESP_LOGE(TAG, "Invalid volume: %d. Must be between 0 and 30.\n", volume);
        return 1;
    }
    ESP_LOGI(TAG, "Playing folder %d, file %d with volume %d\n", folder, file, volume);
    DFPlayerMini_set_volume((uint8_t)volume);
    DFPlayerMini_play_folder((uint8_t)folder, (uint8_t)file);

    return 0;

}

void register_dfplayer_play_folder(void)
{
    play_folder_args_t.folder = arg_int1(NULL, NULL, "<folder_num>", "Folder number (1-2)");
    play_folder_args_t.file = arg_int1(NULL, NULL, "<file_num>", "File number (1-27)");
    play_folder_args_t.volume = arg_int0("v", NULL, "<volume>", "Volume (0-30, default 9)");
    play_folder_args_t.end = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "play_folder",
        .help = "Play a specific file from a folder on the DFPlayer Mini. Usage: play_folder <folder_num> <file_num> [-v <volume>]",
        .hint = NULL,
        .func = &play_folder_cmd,
        .argtable = &play_folder_args_t
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}