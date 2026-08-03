#include "ov7725m12.h"

#include "main.h"

#define OV7725M12_REG_PID          0x0AU
#define OV7725M12_REG_VERSION      0x0BU
#define OV7725M12_SCCB_TIMEOUT_MS  100U
#define OV7725M12_POWER_DELAY_MS   10U
#define OV7725M12_RESET_DELAY_MS   20U

void OV7725M12_PowerDown(void)
{
  HAL_GPIO_WritePin(CAM_RST_GPIO_Port, CAM_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CAM_PWDN_GPIO_Port, CAM_PWDN_Pin, GPIO_PIN_SET);
}

void OV7725M12_ResetAndWake(void)
{
  OV7725M12_PowerDown();
  HAL_Delay(OV7725M12_POWER_DELAY_MS);

  HAL_GPIO_WritePin(CAM_PWDN_GPIO_Port, CAM_PWDN_Pin, GPIO_PIN_RESET);
  HAL_Delay(OV7725M12_POWER_DELAY_MS);

  HAL_GPIO_WritePin(CAM_RST_GPIO_Port, CAM_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(OV7725M12_RESET_DELAY_MS);
}

OV7725M12_Status_t OV7725M12_ReadRegister(I2C_HandleTypeDef *hi2c,
                                           uint8_t reg,
                                           uint8_t *value)
{
  HAL_StatusTypeDef hal_status;

  if ((hi2c == NULL) || (value == NULL))
  {
    return OV7725M12_STATUS_BAD_ARGUMENT;
  }

  /* SCCB reads use separate write-address and read-data phases with a STOP. */
  hal_status = HAL_I2C_Master_Transmit(hi2c,
                                       OV7725M12_I2C_HAL_ADDRESS,
                                       &reg,
                                       1U,
                                       OV7725M12_SCCB_TIMEOUT_MS);
  if (hal_status != HAL_OK)
  {
    return OV7725M12_STATUS_BUS_ERROR;
  }

  hal_status = HAL_I2C_Master_Receive(hi2c,
                                      OV7725M12_I2C_HAL_ADDRESS,
                                      value,
                                      1U,
                                      OV7725M12_SCCB_TIMEOUT_MS);
  if (hal_status != HAL_OK)
  {
    return OV7725M12_STATUS_BUS_ERROR;
  }

  return OV7725M12_STATUS_OK;
}

OV7725M12_Status_t OV7725M12_WriteRegister(I2C_HandleTypeDef *hi2c,
                                            uint8_t reg,
                                            uint8_t value)
{
  uint8_t data[2];

  if (hi2c == NULL)
  {
    return OV7725M12_STATUS_BAD_ARGUMENT;
  }

  data[0] = reg;
  data[1] = value;

  if (HAL_I2C_Master_Transmit(hi2c,
                              OV7725M12_I2C_HAL_ADDRESS,
                              data,
                              sizeof(data),
                              OV7725M12_SCCB_TIMEOUT_MS) != HAL_OK)
  {
    return OV7725M12_STATUS_BUS_ERROR;
  }

  return OV7725M12_STATUS_OK;
}

OV7725M12_Status_t OV7725M12_Probe(I2C_HandleTypeDef *hi2c,
                                    OV7725M12_ID_t *id)
{
  OV7725M12_Status_t status;

  if ((hi2c == NULL) || (id == NULL))
  {
    return OV7725M12_STATUS_BAD_ARGUMENT;
  }

  id->pid = 0U;
  id->version = 0U;

  OV7725M12_ResetAndWake();

  status = OV7725M12_ReadRegister(hi2c, OV7725M12_REG_PID, &id->pid);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  status = OV7725M12_ReadRegister(hi2c,
                                  OV7725M12_REG_VERSION,
                                  &id->version);
  if (status != OV7725M12_STATUS_OK)
  {
    return status;
  }

  if ((id->pid != OV7725M12_EXPECTED_PID) ||
      (id->version != OV7725M12_EXPECTED_VERSION))
  {
    return OV7725M12_STATUS_ID_MISMATCH;
  }

  return OV7725M12_STATUS_OK;
}
