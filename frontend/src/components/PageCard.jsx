function UrlComponent({page}){
    if(!page || page.length === 0)
        return <p>No page yet.</p>

    return (
    <ul>
        <li key={page.url}>
          <a href={page.url} target="_blank" rel="noreferrer">
            {page.title || page.url}
          </a>
          {typeof page.linksCount === "number" && <> · Links: {page.linksCount}</>}
          {page.createdAt && <> · {new Date(page.createdAt).toLocaleString()}</>}
        </li>
    </ul>
  );
}
export default function PageCard({page}){
    return <>
        <UrlComponent page={page}/>
    </>
}