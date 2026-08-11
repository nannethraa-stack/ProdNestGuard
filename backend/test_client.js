async function sendMockTelemetry() {
  try {
    const response = await fetch('http://localhost:5000/api/telemetry', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        device_id: 'portenta_room_01',
        event_type: 'INFANT_CRYING',
        confidence: 0.94,
        max_temp: 38.5,
        ammonia_ppm: 2.1,
        diaper_status: 'SOILED / CHANGE REQUIRED',
        probabilistic_diagnosis: 'Discomfort: Soiled Diaper & Crying (96%)'
      })
    });
    const data = await response.json();
    console.log('Mock telemetry response:', data);
  } catch (err) {
    console.error('Error sending mock telemetry:', err);
  }
}
sendMockTelemetry();