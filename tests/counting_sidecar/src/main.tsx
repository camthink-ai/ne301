import { render } from 'preact'
import LineCountingModule from '@source/pages/applicationManagement/line-counting-module'
import { installFixtureDebug } from '../adapters/bridge'
import './fixture.css'

installFixtureDebug()
render(<LineCountingModule />, document.getElementById('root') as HTMLElement)
