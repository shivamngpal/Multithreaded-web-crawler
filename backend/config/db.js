const mongoose = require("mongoose");
// const dotenv = require("dotenv");

// dotenv.config();    //load env variables to access them

async function connectDatabase(){
    const MONGO_URI = process.env.MONGO_CONNECTION;
    if(!MONGO_URI){
        throw new Error("Missing MongoDB Connection string");
    }
    
    try{
        await mongoose.connect(MONGO_URI);
        console.log("MongoDB connected");
    }catch(e){
        console.log("MongoDB Connection Error Occured : ",e);
        process.exit(1);
    }
}

module.exports = connectDatabase;




