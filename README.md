SidewinderFFB2
==============

C binding for reading the state of, and sending forces to, a Microsoft Sidwinder 
Force Feedback 2.

It currently supports two forces,  a constant force and a "buzzer".
The constant force pull the joystick to specific location. You can adjust the 
power of the pull with the gain on the force. 
The buzzer is just a simple buzzer to notify the user.

This currently only works on win32 platforms.

Usage:

```
import time
import SidewinderFFB2

SidewinderFFB2.init()
SidewinderFFB2.acquire()

# Make sure your holding the joystick to cover the sensor.
# Otherwise the joystick doesn't do the effects.
buzz = SidewinderFFB2.BuzzForce()
buzz.start()

x_y_force = SidewinderFFB2.ConstantForce()
# This will make the forces stronger/weaker.
# The default/mas is SidewinderFFB2.DI_FFNOMINALMAX, which is 10000.
x_y_force.set_gain(7000)

x_y_force.set_direction(SidewinderFFB2.DI_FFNOMINALMAX, SidewinderFFB2.DI_FFNOMINALMAX//2)

time.sleep(2.0)

joy_state = SidewinderFFB2.poll()

print(f"button 5 is pressed : {joy_state.buttons[5]}")
print(f"Joystick X:{joy_state.x} Y:{joy_state.y} throttle: {joy_state.throttle}")
print(f"The hat is pointing : {joy_state.pov}")

# This resets all the force effects loaded on the joystick.
SidewinderFFB2.reset()

SidewinderFFB2.release()
```

