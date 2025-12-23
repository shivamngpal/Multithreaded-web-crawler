import { formatRelativeTime } from "../utils/formatDate.js";

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

  return <>
      {formatRelativeTime(pages[0].createdAt)}
      ({new Date(pages[0].createdAt).toLocaleString()})
  </>
}


export default function StatsCard({pages}){
  return <div className="stats-container">
    <div className="stat-card">
      <h3>Pages Crawled</h3>
      <p>{pages.length}</p>
    </div>

    <div className="stat-card">
      <h3>Seed Domain</h3>
      <p>info.cern.ch</p>
    </div>

    <div className="stat-card">
      <h3>Last Updated</h3>
      <p><LastUpdated pages={pages}/></p>
    </div>
</div>
    // return <>
    //     <PagesCrawled pages={pages}/>
    //     <h3>Seed Domain : http://info.cern.ch</h3>
    //     <LastUpdated pages={pages}/>
    //     <h3>Status : Completed</h3>
    // </>
}