#pragma once
#include "sdk.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t capacity, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
