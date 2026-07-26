const Arrow = () => <span aria-hidden="true" className="diagram-arrow">↓</span>;

export function ConditionDiagram() {
  return <div aria-label="Any から安全な操作までの条件フロー" className="diagram condition-diagram" role="img">
    <div><small>VALUE WORLD</small><strong>Any</strong></div><Arrow />
    <div><small>CHECK + EVIDENCE</small><strong>Condition&lt;P&gt;</strong></div><Arrow />
    <div><small>CAPABILITY CONSUMED</small><strong>Safe operation</strong></div><Arrow />
    <div><small>NEW FACTS</small><strong>Any + conditions</strong></div>
  </div>;
}

export function OwnershipDiagram() {
  return <div aria-label="所有権から move、shared borrow、unique borrow への分岐" className="diagram ownership-diagram" role="img">
    <div className="ownership-root"><small>LINEAR FACT</small><strong>Own(x)</strong></div>
    <div className="ownership-branches"><span>move</span><span>shared borrow</span><span>unique borrow</span></div>
  </div>;
}

export function ProofPipeline() {
  return <div aria-label="Source から Kernel までの Proof IR パイプライン" className="diagram proof-pipeline" role="img">
    {["Source", "CIR", "FIR", "Solver", "PIR", "Kernel"].map((item, index) => <div key={item}><span>{String(index + 1).padStart(2, "0")}</span><strong>{item}</strong></div>)}
  </div>;
}
