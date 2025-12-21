const express = require("express");
const dotenv = require('dotenv');
const mongoose = require("mongoose");
const cors = require("cors");
const {uploadPages, getPages} = require("./routes/pages");
const connectDatabase = require("./config/db");

// load env vars from .env file
dotenv.config();

const app = express();
app.use(cors({
    origin:["http://localhost:5173"]
}));    //enable cors for react app only -> cors() -> enable cors for all origin
app.use(express.json());    // middleware used for reading json from body
// const crawler_url = process.env.CRAWLER_API_URL;

(async ()=>{
    await connectDatabase();
    
    app.post("/api/pages", uploadPages);
    app.get("/api/pages",getPages);
    const PORT = process.env.PORT || 5000;
    app.listen(PORT, ()=>{
        console.log(`Server running on Port : ${PORT}`);
    });
})();
// crawler is a client which can only make HTTP requests
// it only sends data to express server
// we need to make two api endpoints in express server-
// 1. POST - to store received data in mongoDB
// 2. GET - to get the data stored in mongoDB

// if we get a request for data
// then access mongodb instance and get data from there using mongoose
