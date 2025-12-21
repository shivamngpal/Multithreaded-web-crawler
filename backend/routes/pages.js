const mongoose = require("mongoose");
const PagesModel = require("../models/pageSchema");

async function uploadPages(req,res){
    try{
        const url = req.body.url;
        const title = req.body.title;
        const links = req.body.links;
    
        // we are creating a new record in mongoDB everytime any thread sends data to backend
        // backend heavy tasks
        // UPSERT - update DB if record already exists in it, else create a new one

    // THIS WAY CAN LEAD TO RACE CONDITION WHEN TWO THREADS WITH SAME URL TRIES TO CREATE RECORD IN DB AND IT LEADS TO ERROR
    // CAN BE SOLVED BY USING ANOTHER WAY
        // const urlRec = await PagesModel.findOne({url});
        // if(!urlRec){
        //     await PagesModel.create({
        //         url: url,
        //         title: title,
        //         links: links
        //     });
        //     res.status(201).json({
        //     success: true,
        //     message: "Successfully stored in DataBase"
        //     });
        // }
        // else{
        //     await PagesModel.updateOne(
        //         {_id: urlRec._id},
        //         { $set: { title, links }}
        //     );
        //     res.status(200).json({
        //     success: true,
        //     message: "Successfully Updated in DataBase"
        //     });
        // }
        
    // FIX - thread safe -> atomic command as this is one command and each thread executes this one by one
        const result = await PagesModel.updateOne(
            { url },
            { $set: { title, links } },
            { upsert: true }
        );

        const created = (result.upsertedCount || 0) > 0;

        res.status(created ? 201 : 200).json({
            success: true,
            message: created ? "Successfully stored in DataBase" : "Successfully updated in DataBase"
        });

    }catch(e){
        res.status(500).json({
            success: false,
            message: "An Error Occured",
            error: e.message
        });
    }
}

async function getPages(req,res){
    const top50pages = await PagesModel.find({})
                            .select('url title links createdAt')
                            .sort({createdAt: -1})  
                            .limit(50)
                            .exec();    //sorting in descending order(newest first) of createdAt, limit 50 results, exec() -> execute query

    // Transform data to send only url, title, linksCount, and createdAt to frontend
    const records = top50pages.map(page => ({
        url: page.url,
        title: page.title,
        linksCount: page.links ? page.links.length : 0,
        createdAt: page.createdAt
    }));

    res.status(200).json({
        success: true,
        message: "Successfully extracted Data from DB",
        record: records
    });
}

module.exports = { uploadPages, getPages };