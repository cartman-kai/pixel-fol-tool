#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FolLogLevel
{
    FOL_LOG_INFO = 0,
    FOL_LOG_WARNING = 1,
    FOL_LOG_ERROR = 2
} FolLogLevel;

typedef void (*FolLogCallback)(int progress, FolLogLevel level, const char *message, void *user_data);

enum
{
    FOL_SUCCESS = 0,
    FOL_ERROR_INVALID_ARGUMENT = 1,
    FOL_ERROR_OPEN_INPUT = 2,
    FOL_ERROR_OPEN_OUTPUT = 3,
    FOL_ERROR_READ = 4,
    FOL_ERROR_WRITE = 5,
    FOL_ERROR_FORMAT = 6,
    FOL_ERROR_MANIFEST = 7,
    FOL_ERROR_FILESYSTEM = 8
};

int fol_unpack(const char *input_fol, const char *output_dir, FolLogCallback callback, void *user_data);
int fol_pack(const char *input_dir, const char *output_fol, FolLogCallback callback, void *user_data);
const char *fol_result_message(int code);

#ifdef __cplusplus
}
#endif
