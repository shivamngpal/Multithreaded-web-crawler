import { formatRelativeTime } from "../utils/formatDate.js";

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
      <p>http://info.cern.ch</p>
    </div>

    <div className="stat-card">
      <h3>Last Updated</h3>
      <p><LastUpdated pages={pages}/></p>
    </div>
  </div>
}