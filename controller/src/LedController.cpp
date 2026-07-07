#include "LedController.h"

// ---------------- Singleton ----------------
LedController& LedController::instance() {
    static LedController inst;
    return inst;
}

// ---------------- Initialization ----------------
void LedController::init(gpio_num_t led0, gpio_num_t led1, gpio_num_t led2) {
    leds_[0] = led0;
    leds_[1] = led1;
    leds_[2] = led2;

    gpio_config_t cfg = {};
    cfg.pin_bit_mask =
        (1ULL << led0) |
        (1ULL << led1) |
        (1ULL << led2);

    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;

    gpio_config(&cfg);

    allOff();
}

// ---------------- Basic Control ----------------
void LedController::on(int index) {
    if (index >= 0 && index < 3)
        gpio_set_level(leds_[index], 1);
}

void LedController::off(int index) {
    if (index >= 0 && index < 3)
        gpio_set_level(leds_[index], 0);
}

void LedController::allOn() {
    for (int i = 0; i < 3; i++)
        gpio_set_level(leds_[i], 1);
}

void LedController::allOff() {
    for (int i = 0; i < 3; i++)
        gpio_set_level(leds_[i], 0);
}

// ---------------- Blink ----------------
void LedController::blink(int index, uint32_t durationMs) {
    if (index < 0 || index > 2) return;

    if (blinkTimer_) {
        esp_timer_stop(blinkTimer_);
        esp_timer_delete(blinkTimer_);
        blinkTimer_ = nullptr;
    }

    on(index);

    struct Ctx {
        LedController* self;
        int idx;
    };

    Ctx* ctx = new Ctx{ this, index };

    esp_timer_create_args_t args = {
        .callback = [](void* arg) {
            Ctx* ctx = static_cast<Ctx*>(arg);
            ctx->self->off(ctx->idx);
            delete ctx;
        },
        .arg = ctx,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "led_blink",
        .skip_unhandled_events = false
    };

    esp_timer_create(&args, &blinkTimer_);
    esp_timer_start_once(blinkTimer_, durationMs * 1000ULL);
}

// ---------------- Flash ----------------
void LedController::flash(int index, uint32_t onMs, uint32_t offMs) {
    if (index < 0 || index > 2) return;

    if (flashTimer_) {
        esp_timer_stop(flashTimer_);
        esp_timer_delete(flashTimer_);
        flashTimer_ = nullptr;
    }

    struct FlashCtx {
        LedController* self;
        int idx;
        uint32_t onMs;
        uint32_t offMs;
        bool state;
    };

    FlashCtx* ctx = new FlashCtx{ this, index, onMs, offMs, false };

    esp_timer_create_args_t args = {
        .callback = [](void* arg) {
            FlashCtx* ctx = static_cast<FlashCtx*>(arg);

            ctx->state = !ctx->state;
            if (ctx->state)
                ctx->self->on(ctx->idx);
            else
                ctx->self->off(ctx->idx);

            uint64_t next = ctx->state ? ctx->onMs : ctx->offMs;
            esp_timer_start_once(ctx->self->flashTimer_, next * 1000ULL);
        },
        .arg = ctx,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "led_flash",
        .skip_unhandled_events = false
    };

    esp_timer_create(&args, &flashTimer_);
    esp_timer_start_once(flashTimer_, onMs * 1000ULL);
}

// ---------------- Binary ----------------
void LedController::binary(uint8_t value) {
    value &= 0x07;
    for (int i = 0; i < 3; i++) {
        if (value & (1 << i))
            on(i);
        else
            off(i);
    }
}

// ---------------- Chase ----------------
void LedController::chase(uint32_t speedMs) {
    if (chaseTimer_) {
        esp_timer_stop(chaseTimer_);
        esp_timer_delete(chaseTimer_);
        chaseTimer_ = nullptr;
    }

    struct ChaseCtx {
        LedController* self;
        int pos;
        uint32_t speed;
    };

    ChaseCtx* ctx = new ChaseCtx{ this, 0, speedMs };

    esp_timer_create_args_t args = {
        .callback = [](void* arg) {
            ChaseCtx* ctx = static_cast<ChaseCtx*>(arg);

            ctx->self->allOff();
            ctx->self->on(ctx->pos);

            ctx->pos = (ctx->pos + 1) % 3;

            esp_timer_start_once(ctx->self->chaseTimer_, ctx->speed * 1000ULL);
        },
        .arg = ctx,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "led_chase",
        .skip_unhandled_events = false
    };

    esp_timer_create(&args, &chaseTimer_);
    esp_timer_start_once(chaseTimer_, speedMs * 1000ULL);
}

// ---------------- Stop Patterns ----------------
void LedController::stopBlink() {
    if (blinkTimer_) {
        esp_timer_stop(blinkTimer_);
        esp_timer_delete(blinkTimer_);
        blinkTimer_ = nullptr;
    }
}

void LedController::stopFlash() {
    if (flashTimer_) {
        esp_timer_stop(flashTimer_);
        esp_timer_delete(flashTimer_);
        flashTimer_ = nullptr;
    }
}

void LedController::stopChase() {
    if (chaseTimer_) {
        esp_timer_stop(chaseTimer_);
        esp_timer_delete(chaseTimer_);
        chaseTimer_ = nullptr;
    }
}

// ---------------- Test Sequence ----------------
void LedController::testLeds() {
    static int loopCounter = 0;
    static bool flashActive = false;
    static bool chaseActive = false;

    if (loopCounter < 5) {
        on(0);
    }
    else if (loopCounter < 10) {
        blink(1, 500);
    }
    else if (loopCounter < 15) {
        if (!flashActive) {
            flashActive = true;
            flash(2, 200, 800);
        }
    }
    else if (loopCounter < 20) {
        stopFlash();
        binary(5);
    }
    else if (loopCounter < 25) {
        if (!chaseActive) {
            chaseActive = true;
            chase(150);
        }
    }
    else {
        chaseActive = false;
        flashActive = false;
        stopChase();
        loopCounter = 0;
        return;
    }

    loopCounter++;
}