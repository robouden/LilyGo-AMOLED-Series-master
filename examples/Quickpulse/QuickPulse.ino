//
// QuickPulse demo
//
// The Blues QuickPulse module is based on the Safecast SafePulse design, augmented with an Abliq S-35770 that
// performs the task of counting pulses in hardware, rather than relying upon a microcontroller which may or
// may not be able to respond to interrupts quickly enough.
//
// https://www.ablic.com/en/doc/datasheet/counter_ic/S35770_I_E.pdf
//
// The Abliq S-35770 is a 24-bit binary up-counter with an I2C interface that draws only 0.01uA.  When powered
// at 3.3V, it will count accurately at up to 1Mhz.  This means that the QuickPulse's dead time is largely
// determined by the tube and SafePulse, not by the counter.
//
// In practice, any software accessing the QuickPulse can choose how often to sample it.  The Radnote, for
// example, samples the counter at the start of a measurement session (which may be, for example, 5 minutes),
// and then wakes up the MCU at the end of that 5 minutes and reads the count once to determine CPM.
//
// Alternatively, in order to keep a display up to date, the counter may be sampled quite frequently.  It is
// important, though, that the software should NOT reset the counter to 0, which might cause pulses to be
// missed.  Rather, the S-35770's 24-bit counter will simply 'wrap' from 0xFFFFFFh to 0 as it counts, and
// this wrapping should be handled in software.
//

// #include <Wire.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <AceButton.h>

#define ABLIQ_S35770_I2C_ADDRESS        0x32

#define	INTEGRATION_PERIOD_SECS			60
#define	BUCKETS_IN_INTEGRATION_PERIOD	100

// One-time initializations
void setup() {

	// Serial debug output
    Serial.begin(115200);
	uint32_t expires = millis() + 2500;
    while (!Serial && millis() < expires);
    Serial.printf("*** %s %s ***\n", __DATE__, __TIME__);

	// I2C interface is used to access the counter
	// Wire.begin();
	Wire.begin(6,7);
	// LED for feedback
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, LOW);

}

// Continuously-polled
void loop() {

	// Request the value from the counter
	int received, bytesToReceive = 3;
	uint32_t counterValue = 0;
	Wire.requestFrom(ABLIQ_S35770_I2C_ADDRESS, bytesToReceive);
	uint32_t timeoutMs = millis() + 5000;
	for (received=0; received < bytesToReceive && millis() < timeoutMs; ) {
		if (Wire.available()) {
			counterValue = (counterValue << 8) | Wire.read();
			received++;
		}
	}
	if (received < bytesToReceive) {
		Serial.printf("*** timeout trying to access QuickPulse ***\n");
		return;
	}

	// Compute the number of counts since last time, handling wrap, into "elapsedCounts"
	static bool firstTime = true;
    static uint32_t previousCounterValue;
	uint32_t elapsedCounts = 0;
	if (firstTime) {
		firstTime = false;
	} else {
	    if (counterValue >= previousCounterValue) {
	        elapsedCounts = counterValue - previousCounterValue;
	    } else {
	        elapsedCounts = (0x00FFFFFF - previousCounterValue) + counterValue + 1;
	    }
	}
	previousCounterValue = counterValue;
		
	// Maintain CPM on a rolling 1-minute basis.  Note that this code
	// will properly handle a very slow poll rate, "filling out" buckets
	// that have been skipped over if we are not called continuously.
	// This code also handles 32-bit wrap of the millis() counter.
	// Note that I've chosen 60 buckets so that there's responsiveness
	// in the UI, but you can set this number lower if you have less
	// memory and all the counts will still be accurate.
	static uint32_t buckets[BUCKETS_IN_INTEGRATION_PERIOD] = {0};
	uint32_t msPerBucket = (INTEGRATION_PERIOD_SECS * 1000) / (sizeof(buckets)/sizeof(buckets[0]));
	static uint32_t currentBucket = 0;
	static uint32_t oneMinuteElapsed = false;
	uint32_t nowMs = millis();
	static uint32_t bucketBeganMs = 0;
	if (bucketBeganMs == 0) {
		bucketBeganMs = nowMs;
	}
	uint32_t elapsedMsSinceBucketBegan = nowMs - bucketBeganMs;
	if (nowMs < bucketBeganMs) {
		elapsedMsSinceBucketBegan = UINT32_MAX - bucketBeganMs + nowMs + 1;
	}
	uint32_t bucketsToAdvance = elapsedMsSinceBucketBegan / msPerBucket;
	for (uint32_t i=0; i<bucketsToAdvance; i++) {
		if (++currentBucket >= sizeof(buckets)/sizeof(buckets[0])) {
			currentBucket = 0;
			oneMinuteElapsed = true;
		}
		buckets[currentBucket] = 0;
		bucketBeganMs += msPerBucket;
	}
	buckets[currentBucket] += elapsedCounts;

	// Compute CPM
	uint32_t CPM = 0;
	for (uint32_t i=0; i<sizeof(buckets)/sizeof(buckets[0]); i++) {
		CPM += buckets[i];
	}
	CPM *= 60000;
	CPM /= (INTEGRATION_PERIOD_SECS * 1000);

	// For amusement purposes, print the CPM on a periodic basis
	static uint32_t messageLastDisplayedMs = 0;
	if (nowMs > messageLastDisplayedMs + 2500) {
		messageLastDisplayedMs = nowMs;
		if (oneMinuteElapsed) {
			Serial.printf("CPM:%d\n", CPM);
		} else {
			Serial.printf("counting (%d pulses)\n", CPM);
		}
	}

	// For amusement purposes, flash the LED
	for (uint32_t i=0; i<elapsedCounts; i++) {
		digitalWrite(LED_BUILTIN, HIGH);
		delay(5);
		digitalWrite(LED_BUILTIN, LOW);
		delay(5);
	}

}
