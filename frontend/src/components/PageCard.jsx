function UrlComponent({page}){
    if(!page || page.length === 0)
        return <p>No page yet.</p>

    return (
    <div className="page-card">
      <h4>
        <a href={page.url} target="_blank" rel="noreferrer">
          {page.title || "Untitled Page"}
        </a>
      </h4>
      <p>Links: {page.linksCount}</p>
      <p>{new Date(page.createdAt).toLocaleString()}</p>
    </div>
    );
}
export default function PageCard({page}){
    return <>
        <UrlComponent page={page}/>
    </>
}