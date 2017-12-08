import React from 'react';
import GraphBody from '../../../common/Graph/GraphBody';
import PropTypes from 'prop-types';
import { connect } from 'react-redux';
import PointPositionGraph from '../containers/PointPositionGraph';
import styles from '../style/Histogram.scss'

const HistogramCanvas = ({
  data,
  height,
  width,
  unit,
  minValue,
  maxValue
}) => (
    <div>
      <svg width={width} height={height}>
        <GraphBody
          UseLinearGradient={false}
          color={'blue'}
          points={data}
          x={0}
          y={600}
          fill={"blue"}
          fillOpacity={".5"}
          strokeWidth={1}
        />
        <text x={ width - 40} y={height - 10} fontFamily={"Verdana"} fontSize={10} fill={"white"}>
          {unit}
        </text>
      </svg>
        <PointPositionGraph className={styles.Envelope}
          width={width}
          height={height}
          minValue={minValue}
          maxValue={maxValue}
        />
    </div>
);
HistogramCanvas.PropTypes = {
  data: PropTypes.arrayOf(
    PropTypes.shape({
      x: PropTypes.number.isRequired,
      y:PropTypes.number.isRequired,
    })
  ).isRequired
}
export default HistogramCanvas;
