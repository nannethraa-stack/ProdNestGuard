const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const sqlite3 = require('sqlite3').verbose();
const path = require('path');

const app = express();
const server = http.createServer(app);
const io = new Server(server);

const PORT = process.env.PORT || 5000;

// Middleware to parse JSON telemetry payloads
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Initialize Local SQLite Database
const dbPath = path.join(__dirname, 'database.sqlite');
const db = new sqlite3.Database(dbPath, (err) => {
    if (err) {
        console.error('Database connection error:', err.message);
    } else {
        console.log('Connected to local SQLite database.');
    }
});

// Create telemetry table if it doesn't exist (with expanded vision tracking support)
db.serialize(() => {
    db.run(`CREATE TABLE IF NOT EXISTS telemetry (
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
});

// Telemetry ingestion endpoint from microcontrollers / hardware nodes
app.post('/api/telemetry', (req, res) => {
    const { 
        device_id, 
        event_type, 
        confidence, 
        max_temp, 
        ammonia_ppm, 
        diaper_status, 
        probabilistic_diagnosis 
    } = req.body;

    const query = `INSERT INTO telemetry 
        (device_id, event_type, confidence, max_temp, ammonia_ppm, diaper_status, probabilistic_diagnosis) 
        VALUES (?, ?, ?, ?, ?, ?, ?)`;

    db.run(query, [
        device_id || 'portenta_room_01', 
        event_type || 'MONITORING', 
        confidence || 0.0, 
        max_temp || 0.0, 
        ammonia_ppm || 0.0, 
        diaper_status || 'NORMAL', 
        probabilistic_diagnosis || 'Normal State'
    ], function(err) {
        if (err) {
            console.error('Database insert error:', err.message);
            return res.status(500).json({ error: err.message });
        }

        // Broadcast the telemetry update live to all connected dashboard clients via WebSockets
        io.emit('telemetry_update', req.body);

        res.status(200).json({ success: true, id: this.lastID });
    });
});

// Real-time WebSocket connection tracking
io.on('connection', (socket) => {
    console.log(`Dashboard client connected: ${socket.id}`);

    socket.on('disconnect', () => {
        console.log(`Dashboard client disconnected: ${socket.id}`);
    });
});

// Start Express Server
server.listen(PORT, () => {
    console.log(`NestGuard Backend Server running on port ${PORT}`);
});
