require('dotenv').config();
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const cors = require('cors');
const sqlite3 = require('sqlite3').verbose();
const path = require('path');

const app = express();
const server = http.createServer(app);
const io = new Server(server, { cors: { origin: "*" } });

app.use(express.json());
app.use(cors());

// Serve static frontend dashboard from the 'public' folder
app.use(express.static(path.join(__dirname, 'public')));

// Initialize SQLite Database and Schema
const db = new sqlite3.Database('./nestguard.db', (err) => {
    if (err) console.error('Database connection error:', err.message);
    else console.log('Connected to local SQLite database.');
});

db.run(`CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT,
    event_type TEXT,
    confidence REAL,
    max_temp REAL,
    ammonia_ppm REAL,
    diaper_status TEXT,
    probabilistic_diagnosis TEXT,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
)`);

// Telemetry endpoint for Portenta H7 or simulation client
app.post('/api/telemetry', (req, res) => {
    const { device_id, event_type, confidence, max_temp, ammonia_ppm, diaper_status, probabilistic_diagnosis } = req.body;

    if (!device_id) {
        return res.status(400).json({ error: 'Missing required device_id field' });
    }

    const query = `INSERT INTO events (device_id, event_type, confidence, max_temp, ammonia_ppm, diaper_status, probabilistic_diagnosis) VALUES (?, ?, ?, ?, ?, ?, ?)`;
    db.run(query, [
        device_id, 
        event_type || 'MONITORING', 
        confidence || 0.0, 
        max_temp !== undefined ? max_temp : null, 
        ammonia_ppm !== undefined ? ammonia_ppm : 0.0, 
        diaper_status || 'NORMAL',
        probabilistic_diagnosis || 'Normal State'
    ], function(err) {
        if (err) return res.status(500).json({ error: err.message });

        // Broadcast real-time event to connected dashboards via WebSockets
        io.emit('live_alert', {
            id: this.lastID,
            device_id,
            event_type: event_type || 'MONITORING',
            confidence: confidence || 0.0,
            max_temp,
            ammonia_ppm,
            diaper_status,
            probabilistic_diagnosis: probabilistic_diagnosis || 'Normal State',
            timestamp: new Date().toISOString()
        });

        console.log(`[TELEMETRY] Device ${device_id} -> Diagnosis: ${probabilistic_diagnosis}`);
        return res.status(200).json({ status: 'success', event_id: this.lastID });
    });
});

// Endpoint to fetch recent logs for dashboard history
app.get('/api/events', (req, res) => {
    db.all(`SELECT * FROM events ORDER BY timestamp DESC LIMIT 50`, [], (err, rows) => {
        if (err) return res.status(500).json({ error: err.message });
        res.json(rows);
    });
});

// WebSocket connection handler
io.on('connection', (socket) => {
    console.log('Dashboard client connected:', socket.id);
    socket.on('disconnect', () => {
        console.log('Dashboard client disconnected:', socket.id);
    });
});

const PORT = process.env.PORT || 5000;
server.listen(PORT, () => {
    console.log(`NestGuard Backend Server running on port ${PORT}`);
});