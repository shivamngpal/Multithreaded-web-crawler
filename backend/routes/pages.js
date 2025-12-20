const mongoose = require("mongoose");
const PagesModel = require("../models/pageSchema");

async function uploadPages(req,res){
    const url = req.body.url;
    const title = req.body.title;
    const links = req.body.links;

    await PagesModel.create({
        url: url,
        title: title,
        links: links
    });

    res.status(200).json({
        success: true,
        message: "Successfully stored in DataBase"
    });
}

async function getPages(req,res){
    const top50pages = await PagesModel.find({})
                            .sort({createdAt: -1})  
                            .limit(50)
                            .exec();    //sorting in descending order(newest first) of createdAt, limit 50 results, exec() -> execute query

    res.status(200).json({
        success: true,
        message: "Successfully extracted Data from DB",
        record: top50pages
    });
}

module.exports = { uploadPages, getPages };