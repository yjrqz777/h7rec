# C Language Coding Rules

## Scope

- These rules apply to C source files, headers, and project directories created or modified in this repository.
- Keep naming and formatting consistent across the project. Names must clearly describe their purpose and module ownership.
- Do not rename unrelated existing code solely to enforce these rules unless the task explicitly requests a cleanup.
- Third-party libraries, standard protocol stacks, generated files, and customer-provided files are exempt unless the task explicitly requires modifying them.
- Avoid giving local and file-scope variables the same name.

## Project Layout

- Name user-owned program directories using the established forms: `UserApp`, `UserBsp`, and `UserDrv`.
- Every project must provide and use `User_global.h` and `user_config.h`. These two required names are explicit exceptions to the general lowercase file-name rule.

## File Names

- Name C source and header files with lowercase words separated by underscores.
- Examples: `user_app.c`, `user_app.h`, `bsp_motor.c`, `bsp_motor.h`, `drv_pwm.c`, and `drv_pwm.h`.

## Source And Header Organization

- Put all `#include` directives together at the beginning of each C file. Do not place includes among variable definitions, function definitions, or implementation code.
- Avoid defining macros in C files. Put macros that are used or reused by a module in the corresponding header.
- Write integer, hexadecimal, and floating-point literal macro values inside parentheses.
- When a macro needs a comment, use a trailing `/* ... */` block comment on the same line.
- Derive each header guard from the file name by converting it to uppercase underscore form and adding leading and trailing double underscores. For example, `user_time.h` uses `__USER_TIME_H__`.

```c
/* user_app.c */
#include "user_app.h"
#include "User_global.h"
```

```c
/* user_app.h */
#ifndef __USER_APP_H__
#define __USER_APP_H__

#define FRAME_SOF       (0xAAu)    /* Frame start */
#define RETRY_COUNT     (3u)       /* Retry count */
#define SPEED_LIMIT     (1000.0f)  /* Speed limit */

#endif /* __USER_APP_H__ */
```

## Function Names

- Functions declared or defined in `user_xxx.c` or `user_xxx.h` use `Usr` + module name + function name in PascalCase, for example `UsrSensorRead()`.
- Functions declared or defined in `bsp_xxx.c` or `bsp_xxx.h` use `Bsp` + module name + function name in PascalCase, for example `BspMotorSetSpeed()`.
- Functions declared or defined in `drv_xxx.c` or `drv_xxx.h` use `Drv` + module name + function name in PascalCase, for example `DrvPwmSetDutyCycle()`.

## Data Types

- Use the standard types made available by `User_global.h`.
- Use `uint8_t`, `int8_t`, `uint16_t`, `int16_t`, `uint32_t`, `int32_t`, `uint64_t`, `int64_t`, `float`, and `double` as the basic numeric types.
- Use these type prefixes in identifiers:
  - `uint8_t`: `u8`
  - `uint16_t`: `u16`
  - `uint32_t`: `u32`
  - `uint64_t`: `u64`
  - `int8_t`: `s8`
  - `int16_t`: `s16`
  - `int32_t`: `s32`
  - `int64_t`: `s64`
  - `float`: `f32`
  - `double`: `f64`

## Structures

- Name structure typedefs as `t` + PascalCase name + `Def`, for example `tUserFilterDef`.
- Name structure variables as `t` + PascalCase name, for example `tUserFilter`.
- Name structure pointer variables as `pt` + PascalCase name, for example `ptUserFilter`.

```c
typedef struct tUserFilterDef
{
    uint16_t u16BufferSize;
    uint32_t u32SumValue;
} tUserFilterDef;

tUserFilterDef * ptUserFilter;
tUserFilterDef tUserFilter;
```

## Unions

- Name union typedefs as `u` + PascalCase name + `Def`, for example `uDeviceDataDef`.
- Name union variables as `u` + PascalCase name, for example `uDeviceData`.
- Name union pointer variables as `pu` + PascalCase name, for example `puDeviceData`.

```c
typedef union uDeviceDataDef
{
    uint32_t u32Value;
    uint8_t u8Bytes[4];
} uDeviceDataDef;

uDeviceDataDef * puDeviceData;
uDeviceDataDef uDeviceData;
```

## Enumerations

- Name enumeration typedefs as `e` + PascalCase name + `Def`, for example `eFilterModeDef`.
- Name enumeration members as `E_` + uppercase words separated by underscores.
- Name enumeration variables as `e` + PascalCase name, for example `eCurrentMode`.

```c
typedef enum
{
    E_FILTER_MODE_AVERAGE = 0,
    E_FILTER_MODE_MEDIAN,
    E_FILTER_MODE_SLIDING
} eFilterModeDef;

eFilterModeDef eCurrentMode;
```

## Macros

- Name macros with uppercase words separated by underscores.
- Parenthesize literal macro values.
- Put any macro comment in `/* ... */` form at the end of the same line.

```c
#define FRAME_SOF (0xAAu) /* Frame start */
#define FRAME_EOF (0x55u) /* Frame end */
```

## Variables And Parameters

- Name global and file-scope static scalar variables as the type prefix + PascalCase description.
- Examples: `uint32_t u32TotalCount`, `uint16_t u16TotalCount`, and `static uint8_t u8TotalCount`.
- Name signed scalar variables using `s8`, `s16`, `s32`, or `s64` + PascalCase description.
- Name floating-point scalar variables using `f32` or `f64` + PascalCase description.
- Name non-pointer scalar function parameters as the type prefix + PascalCase description, for example `uint16_t u16Speed` and `uint8_t u8Duty`.
- Name non-structure pointer variables as `p` + type prefix + PascalCase description.
- Apply the same pointer rule to pointer parameters. Examples: `uint8_t * pu8DataBuf` and `uint32_t * pu32DataBuf`.
- Name ordinary function-local scalar variables in PascalCase without a type prefix, for example `TotalAmount` and `Index`.
- The short loop indices `i`, `j`, and `k` are allowed.
- Structure, union, and enumeration variables follow their dedicated rules above instead of scalar type-prefix rules.

## Reference Function Declarations

```c
/* user_app.c */
void UsrFilterProcessData(void);

/* bsp_motor.c */
void BspMotorSetSpeed(uint16_t u16Speed);

/* drv_pwm.c */
void DrvPwmSetDutyCycle(uint8_t u8Duty);
```

## Review Checklist

When creating or reviewing C code, verify that:

- Project-owned directories and file names follow the required naming schemes.
- Includes are grouped at the beginning of each C file.
- Reusable macros are in headers, literal macro values are parenthesized, and macro comments use the required format.
- Header guards match the header file name.
- Functions use the prefix associated with their module layer.
- Structures, unions, enumerations, variables, pointers, and parameters use the correct prefixes and casing.
- Names are descriptive, consistent, module-oriented, and do not create avoidable scope conflicts.

## Doxygen Comments

- Use Doxygen block comments in the `/** ... */` form. Do not use `///`.
- Write documentation text in concise English and end complete sentences with a period.
- Keep comments synchronized with behavior whenever related code changes.
- Describe intent, constraints, ownership, side effects, and failure conditions. Do not restate the code.
- Do not add `@author`, `@date`, version history, or change-log fields.

### File Documentation

- Add a Doxygen file comment to every project-owned `.c` and `.h` file.
- Place the file comment before all `#include` directives and header guards.
- Use `@file` and `@brief`. Add `@details` only when the module requires additional context.
- Generated files, third-party libraries, and customer-provided files remain exempt.

```c
/**
 * @file app_camera.h
 * @brief Provides camera capture and LVGL preview interfaces.
 */
```

### Function Documentation

- Document every public function above its definition in the `.c` file.
- Do not add function documentation to declarations in header files.
- Document a file-local `static` function only when its behavior, constraints, side effects, or algorithm are not obvious.
- Do not document simple getters, setters, wrappers, or self-explanatory helpers unless they have important constraints.
- Start with a concise `@brief` sentence without repeating the function name.
- List parameters in declaration order.
- Mark every parameter as `@param[in]`, `@param[out]`, or `@param[in,out]`.
- Use `@return` for a general return-value description.
- Use `@retval` when individual return values have distinct meanings.
- Do not add `@return` or `@retval` to a function returning `void`.
- Use `@note` for important usage constraints and `@warning` for conditions that can cause data loss, corruption, deadlock, or hardware damage.

```c
/* user_camera.c */
/**
 * @brief Starts capture of one camera frame.
 * @param[in] ptDcmi Pointer to the initialized DCMI handle.
 * @param[out] pu32ElapsedMs Pointer receiving the capture duration.
 * @retval 0 The frame was captured successfully.
 * @retval 1 The capture failed or timed out.
 * @note The destination frame buffer must be cache-line aligned.
 */
uint8_t UsrCameraCaptureFrame(DCMI_HandleTypeDef * ptDcmi,
                              uint32_t * pu32ElapsedMs)
{
    /* Implementation. */
}
```

### Types And Members

- Add a Doxygen comment above public structures, unions, enumerations, and typedefs.
- Document members and enumeration values with trailing `/**< ... */` comments.
- Do not document private members whose meaning is already clear from their names.

```c
/**
 * @brief Describes the current camera capture state.
 */
typedef enum
{
    E_CAMERA_CAPTURE_IDLE = 0, /**< No capture is active. */
    E_CAMERA_CAPTURE_BUSY,     /**< A frame capture is in progress. */
    E_CAMERA_CAPTURE_DONE,     /**< The frame is ready for processing. */
    E_CAMERA_CAPTURE_ERROR     /**< The most recent capture failed. */
} eCameraCaptureStateDef;
```

### Macros And Local Comments

- Keep ordinary macro comments in the existing trailing `/* ... */` format.
- Use Doxygen comments for macros only when they are part of the documented public API.
- Use ordinary `/* ... */` comments inside functions for non-obvious algorithms, hardware constraints, synchronization, cache handling, or register sequences.
- Do not use Doxygen comments for local variables or individual implementation steps.
