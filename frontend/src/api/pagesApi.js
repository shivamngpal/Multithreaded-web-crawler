const backendUrl = import.meta.env.VITE_CRAWLER_URL || "http://localhost:5000";
if(!backendUrl)
    throw new Error("Backend URL not configured");

export async function getPages(){
    const res = await fetch(`${backendUrl}/api/pages`);
    if(!res.ok)
        throw new Error(`Request Failed : ${res.status}`);
    
    const data = await res.json();
    return data.record;
}