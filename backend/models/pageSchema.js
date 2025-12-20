const mongoose = require("mongoose");
const Schema = mongoose.Schema;

const PageSchema = new Schema({
    url : {type: String, required: true, trim: true, unique: true},
    title : {type: String, trim: true},
    links : {type: [String], default: []}
},{timestamps: true});

// use singular model name; collection will be 'pages'
const PagesModel = mongoose.model('page',PageSchema);

module.exports = PagesModel;