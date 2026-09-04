/**
 * @file interface.h
 * @brief Abstract servo driver interface.
 */

#pragma once

namespace driver::servo
{

/**
 * @brief Abstract interface for servo control.
 */
class Interface
{
public:
    virtual ~Interface() noexcept = default;

    /**
     * @brief Initialize the servo driver.
     * @return True if initialization succeeded.
     */
    virtual bool init() noexcept = 0;

    /**
     * @brief Deinitialize the servo driver.
     * @return True if deinitialization succeeded.
     */
    virtual bool deinit() noexcept = 0;

    /**
     * @brief Check if the driver is initialized.
     * @return True if ready to use.
     */
    virtual bool isInitialized() const noexcept = 0;

    /**
     * @brief Set servo direction in degrees.
     * @param[in] angleDegrees Desired servo angle. This can theoretically be any value, from -90 (rotate left) to 90 (rotate right),
     * but the implementation may limit the range to a specific min/max.
     * @return True if accepted.
     */
    virtual bool setDirection(float angleDegrees) noexcept = 0;

    /**
     * @brief Get the current servo angle in degrees.
     * @return Current servo angle.
     */
    virtual float getDirection() const noexcept = 0;

    /**
     * @brief Center the servo.
     * @return True if accepted.
     */
    virtual bool center() noexcept = 0;

    // No copy and move operations allowed
    Interface(const Interface&)            = delete;
    Interface& operator=(const Interface&) = delete;
    Interface(Interface&&)                 = delete;
    Interface& operator=(Interface&&)      = delete;

protected:
    Interface() noexcept = default;
};

} // namespace driver::servo
