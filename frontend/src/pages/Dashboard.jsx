import { useState } from "react"
import { useEffect } from "react"
import {getPages} from "../api/pagesApi.js"
import StatsCard from "../components/StatsCard.jsx"
import Loader from "../components/Loader.jsx"

// heading
// cards -> stats and pages
// stats - 1, pages - multiple


export default function Dashboard(){
    const [pages,setPages]= useState([]);
    const [loading,setLoading]= useState(false);
    const [error,setError]= useState(null);

    useEffect(()=>{
        const fetchPages = async () => {
            setLoading(true);
            setError(null);
            try{
                const data = await getPages();
                setPages(data); //data contains record array
            }catch(err){
                setError(err.message);
            }finally{
                setLoading(false);
            }
        }
        fetchPages();
    },[]);

    if(loading) return <Loader/>;
    if(error) return <h2>Error : {error}</h2>

    return <>
        <Header/>
        <h1>Statistics Card</h1>
        <StatsCard pages={pages}/>
    </>
}

function Header(){
    return <>
    <h1>Multithreaded Web Crawler Dashboard</h1>
    <h3>Seed Url : http://info.cern.ch</h3>
    <h3>Built in C++ (libcurl + Gumbo) with MERN backend</h3>
    </>
}