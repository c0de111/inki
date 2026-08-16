# Power consumption estimation for a microcontroller circuit with DS3231 RTC
# Daily-resolution, but output in yearly format

# === CONFIGURATION PARAMETERS ===

# Battery capacity (3x AA Alkaline)
# capacity_total_mAh = 2500

# Battery capacity (3x AAA Alkaline)
capacity_total_mAh = 1200

# Idle mode current (only RTC powered via VBAT path)
idle_current_uA = 5  # µA

# Measured current consumption per activation (includes MCU + peripherals for 1 wake-up), from measurements on real devices. Both 4.2" as wll as 7.5" show about the same consumption (ie, the epaper refresh is not a big contribution to consumption). This includes the LED switched ON.
consumption_per_activation_mAh = 0.25

# LED parameters
led_voltage_supply = 3.3     # V
led_forward_voltage = 2.0    # V
led_resistor_ohm = 220       # Ohm
activation_duration_s = 13   # seconds

# === CALCULATE LED ENERGY ===
led_current_mA = 1e3 * (led_voltage_supply - led_forward_voltage) / led_resistor_ohm
led_consumption_per_activation_mAh = led_current_mA * (activation_duration_s / 3600)

# Wake-up configuration
activations_per_day = 288         # Number of wake-ups per day
workdays_per_week = 7             # Active days per week
days_per_week = 7

# Battery self-discharge rate (typical for Alkaline)
self_discharge_percent = 2.5      # in percent per year
self_discharge_daily = self_discharge_percent / 100 / 365  # daily rate

# === DAILY CALCULATIONS ===

# Daily fixed consumption
idle_current_mA = idle_current_uA / 1000
idle_consumption_per_day_mAh = idle_current_mA * 24
active_consumption_per_day_mAh = consumption_per_activation_mAh * activations_per_day

# Determine if a given day is a working day (Mon–Fri)
def is_working_day(day):
    return (day % days_per_week) < workdays_per_week

# Loop to calculate day-by-day consumption with exponential self-discharge
remaining_capacity = capacity_total_mAh
day = 0
year = 0
daily_stats = []
yearly_stats = []

accumulated_fixed = 0
accumulated_self = 0

while remaining_capacity > 0:
    day += 1
    active_today = is_working_day(day)
    active_use = active_consumption_per_day_mAh if active_today else 0
    self_discharge = remaining_capacity * self_discharge_daily
    fixed_consumption = idle_consumption_per_day_mAh + active_use
    total_use = fixed_consumption + self_discharge

    remaining_capacity -= total_use
    accumulated_fixed += fixed_consumption
    accumulated_self += self_discharge

    if day % 365 == 0 or remaining_capacity <= 0:
        year += 1
        yearly_stats.append((year, accumulated_self, accumulated_fixed))
        accumulated_self = 0
        accumulated_fixed = 0

# === OUTPUT ===

print("=== Power Consumption Estimate ===\n")
print("Used Parameters:")
print(f"- Idle current: {idle_current_uA} µA (RTC consumption, residual current via pullups and others)")
print(f"- Activation consumption: {consumption_per_activation_mAh:.3f} mAh per wake-up, same for 4.2 and 7.5 version")
print(f"- Calculated LED consumption, included in the above: {led_consumption_per_activation_mAh:.3f} mAh per wake-up")
print(f"- Wake-up schedule: {activations_per_day}x per day, {workdays_per_week} days/week")
print(f"- Battery self-discharge: {self_discharge_percent}% per year")
print(f"- Battery capacity: {capacity_total_mAh} mAh\n")

print("Annualized Consumption Rates (approximate):")
print(f"- Idle: {idle_consumption_per_day_mAh * 365:.1f} mAh/year")
print(f"- Active (consumption by circuit activations): {active_consumption_per_day_mAh * (workdays_per_week / days_per_week) * 365:.1f} mAh/year\n")

print(f"=> Estimated battery lifetime: {day} days (≈ {day / 365:.2f} years)")

print("\nYearly Breakdown:")
print("Year | Self-Discharge (mAh) | Fixed Consumption (mAh)")
for year, sd, fixed in yearly_stats:
    print(f"{year:4} | {sd:21.2f} | {fixed:21.2f}")
