function PagesCrawled({pages}){
    const pageNum = pages.length;
    return <>
        <h3>Pages Crawled : {pageNum}</h3>
    </>
}

function LastUpdated({ pages }) {
  if (pages.length === 0) return <h3>Last Updated: N/A</h3>;

//   const maxUpdate = pages.reduce((latest, page) => {
//     return new Date(page.createdAt) > new Date(latest)
//       ? page.createdAt
//       : latest;
//   }, pages[0].createdAt);

  return <h3>Last Updated: {new Date(pages[0].createdAt).toLocaleString()}</h3>;
}


export default function StatsCard({pages}){
    return <div>
        <PagesCrawled pages={pages}/>
        <h3>Seed Domain : http://info.cern.ch</h3>
        <LastUpdated pages={pages}/>
        <h3>Status : Completed</h3>
    </div>
}