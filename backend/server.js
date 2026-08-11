db.run(`CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT,
    event_type TEXT,
    confidence REAL,
    max_temp REAL,
    ammonia_ppm REAL,
    diaper_status TEXT,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
)`);

app.post('/api/telemetry', (req, res) => {
    const { device_id, event_type, confidence, max_temp, ammonia_ppm, diaper_status } = req.body;

    if (!device_id) {
        return res.status(400).json({ error: 'Missing required device_id field' });
    }

    const query = `INSERT INTO events (device_id, event_type, confidence, max_temp, ammonia_ppm, diaper_status) VALUES (?, ?, ?, ?, ?, ?)`;
    db.run(query, [device_id, event_type || 'MONITORING', confidence || 0.0, max_temp || null, ammonia_ppm || 0.0, diaper_status || 'NORMAL'], function(err) {
        if (err) return res.status(500).json({ error: err.message });

        io.emit('live_alert', {
            id: this.lastID,
            device_id,
            event_type: event_type || 'MONITORING',
            confidence: confidence || 0.0,
            max_temp,
            ammonia_ppm,
            diaper_status,
            timestamp: new Date().toISOString()
        });

        return res.status(200).json({ status: 'success', event_id: this.lastID });
    });
});
